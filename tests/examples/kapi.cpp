// =============================================================================
// cryptocogs — A type-safe C++ library for the Kraken and Binance Spot REST and WebSocket APIs
//
// Copyright (c) 2026 Rob Tomlin
//
// Licensed under the MIT License. See LICENSE file in the project root for
// full license information.
// =============================================================================

#include "kapi.hpp"

#include <openssl/buffer.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/bio.h>

#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <sstream>
#include <cstring>
#include <ctime>
#include <cerrno>
#include <fstream>
#include <memory>

#define CURL_VERBOSE 0L //1L = enabled, 0L = disabled

//------------------------------------------------------------------------------

namespace Kraken {

//------------------------------------------------------------------------------
// helper function to compute SHA256:
static std::vector<unsigned char> sha256(const std::string& data)
{
   std::vector<unsigned char> digest(SHA256_DIGEST_LENGTH);

   auto ctx = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>(
      EVP_MD_CTX_new(), EVP_MD_CTX_free
   );
   EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr);
   EVP_DigestUpdate(ctx.get(), data.c_str(), data.length());
   EVP_DigestFinal_ex(ctx.get(), digest.data(), nullptr);

   return digest;
}

//------------------------------------------------------------------------------
// helper function to decode a base64 string to a vector of bytes:
static std::vector<unsigned char> b64_decode(const std::string& data)
{
   BIO* b64 = BIO_new(BIO_f_base64());
   BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

   BIO* bmem = BIO_new_mem_buf((void*)data.c_str(),data.length());
   bmem = BIO_push(b64, bmem);

   std::vector<unsigned char> output(data.length());
   int decoded_size = BIO_read(bmem, output.data(), output.size());
   BIO_free_all(bmem);

   if (decoded_size < 0)
      throw std::runtime_error("failed while decoding base64.");

   return output;
}

//------------------------------------------------------------------------------
// helper function to encode a vector of bytes to a base64 string:
static std::string b64_encode(const std::vector<unsigned char>& data)
{
   BIO* b64 = BIO_new(BIO_f_base64());
   BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

   BIO* bmem = BIO_new(BIO_s_mem());
   b64 = BIO_push(b64, bmem);

   BIO_write(b64, data.data(), data.size());
   BIO_flush(b64);

   BUF_MEM* bptr = NULL;
   BIO_get_mem_ptr(b64, &bptr);

   std::string output(bptr->data, bptr->length);
   BIO_free_all(b64);

   return output;
}

//------------------------------------------------------------------------------
// helper function to hash with HMAC algorithm:
static std::vector<unsigned char>
hmac_sha512(const std::vector<unsigned char>& data,
	    const std::vector<unsigned char>& key)
{
   size_t len = EVP_MAX_MD_SIZE;
   std::vector<unsigned char> digest(len);

   auto mac = std::unique_ptr<EVP_MAC, decltype(&EVP_MAC_free)>(
      EVP_MAC_fetch(nullptr, "HMAC", nullptr), EVP_MAC_free
   );

   if (!mac) {
      throw std::runtime_error("cannot fetch HMAC");
   }

   auto ctx = std::unique_ptr<EVP_MAC_CTX, decltype(&EVP_MAC_CTX_free)>(
      EVP_MAC_CTX_new(mac.get()), EVP_MAC_CTX_free
   );

   if (!ctx) {
      throw std::runtime_error("cannot create EVP_MAC_CTX");
   }

   OSSL_PARAM params[] = {
      OSSL_PARAM_construct_utf8_string("digest", (char*)"SHA512", 0),
      OSSL_PARAM_construct_end()
   };

   EVP_MAC_init(ctx.get(), key.data(), key.size(), params);
   EVP_MAC_update(ctx.get(), data.data(), data.size());
   EVP_MAC_final(ctx.get(), digest.data(), &len, digest.size());

   return digest;
}


//------------------------------------------------------------------------------
// builds a query string from KAPI::Input (a=1&b=2&...)
static std::string build_query(const KAPI::Input& input)
{
   std::ostringstream oss;
   KAPI::Input::const_iterator it = input.begin();
   for (; it != input.end(); ++it) {
      if (it != input.begin()) oss << '&';  // delimiter
      oss << it->first <<'='<< it->second;
   }

   return oss.str();
}

//------------------------------------------------------------------------------
// helper function to create a nonce:
static std::string create_nonce()
{
   std::ostringstream oss;

   timeval tp{};
   if (gettimeofday(&tp, NULL) != 0) {
      oss << "gettimeofday() failed: " << strerror(errno);
      throw std::runtime_error(oss.str());
   }
   else {
      // format output string
      oss << std::setfill('0')
	  << std::setw(10) << tp.tv_sec
	  << std::setw(6)  << tp.tv_usec;
   }
   return oss.str();
}

Keys load_keys(const std::string& name, const std::string& location) {
    std::string dir = location.empty()
        ? std::string(getenv("HOME")) + "/.kraken"
        : location;

    std::string filepath = dir + "/" + name;

    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open key file: " + filepath);
    }

    Keys keys;
    if (!std::getline(file, keys.apiKey) || keys.apiKey.empty()) {
        throw std::runtime_error("Missing or empty API key in: " + filepath);
    }
    if (!std::getline(file, keys.privateKey) || keys.privateKey.empty()) {
        throw std::runtime_error("Missing or empty private key in: " + filepath);
    }

    return keys;
}

//------------------------------------------------------------------------------
// constructor with all explicit parameters
KAPI::KAPI(const std::string& key, const std::string& secret,
	   const std::string& url, const std::string& version)
   :key_(key), secret_(secret), url_(url), version_(version)
{
   init();
}

//------------------------------------------------------------------------------
// default API base URL and API version
KAPI::KAPI(const std::string& key, const std::string& secret)
   :key_(key), secret_(secret), url_("https://api.kraken.com"), version_("0")
{
   init();
}

//------------------------------------------------------------------------------
// constructor with empty API key and API secret
KAPI::KAPI()
   :key_(""), secret_(""), url_("https://api.kraken.com"), version_("0")
{
   init();
}

//------------------------------------------------------------------------------
// initializes libcurl:
void KAPI::init()
{
   curl_ = curl_easy_init();
   if (curl_) {
      curl_easy_setopt(curl_, CURLOPT_VERBOSE, CURL_VERBOSE);
      curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 1L);
      curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 2L);
      curl_easy_setopt(curl_, CURLOPT_USERAGENT, "Kraken C++ API Client");
      curl_easy_setopt(curl_, CURLOPT_POST, 1L);
      // set callback function
      curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, KAPI::write_cb);
   }
   else {
      throw std::runtime_error("can't create curl handle");
   }
}

//------------------------------------------------------------------------------
// destructor:
KAPI::~KAPI()
{
   curl_easy_cleanup(curl_);
}

//------------------------------------------------------------------------------
// returns message signature generated from a URI path, a nonce
// and postdata, message signature is created as a follows:
//
//   hmac_sha512(path + sha256(nonce + postdata), b64decode(secret))
//
// and the result is converted in a base64 string:
std::string KAPI::signature(const std::string& path,
			    const std::string& nonce,
			    const std::string& postdata) const
{
   // add path to data to encrypt
   std::vector<unsigned char> data(path.begin(), path.end());

   // concatenate nonce and postdata and compute SHA256
   std::vector<unsigned char> nonce_postdata = sha256(nonce + postdata);

   // concatenate path and nonce_postdata (path + sha256(nonce + postdata))
   data.insert(data.end(), nonce_postdata.begin(), nonce_postdata.end());

   // and compute HMAC
   return b64_encode( hmac_sha512(data, b64_decode(secret_)) );
}

//------------------------------------------------------------------------------
// CURL write function callback:
size_t KAPI::write_cb(char* ptr, size_t size, size_t nmemb, void* userdata)
{
   std::string* response = reinterpret_cast<std::string*>(userdata);
   size_t real_size = size * nmemb;

   response->append(ptr, real_size);
   return real_size;
}

//------------------------------------------------------------------------------
// deals with public API methods:
std::string KAPI::public_method(const std::string& method,
				const KAPI::Input& input) const
{
   // build method URL
   std::string path = "/" + version_ + "/public/" + method;
   std::string method_url = url_ + path + "?" + build_query(input);
   curl_easy_setopt(curl_, CURLOPT_URL, method_url.c_str());

   // reset the http header
   curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, NULL);

   // where CURL write callback function stores the response
   std::string response;
   curl_easy_setopt(curl_, CURLOPT_WRITEDATA, static_cast<void*>(&response));

   // Set GET method
   curl_easy_setopt(curl_, CURLOPT_HTTPGET, 1L);

   // perform CURL request
   CURLcode result = curl_easy_perform(curl_);
   if (result != CURLE_OK) {
      std::ostringstream oss;
      oss << "curl_easy_perform() failed: "<< curl_easy_strerror(result);
      throw std::runtime_error(oss.str());
   }

   return response;
}

std::string KAPI::public_method(const std::string& method) const
{
   KAPI::Input input;
   return public_method(method, input);
}

//------------------------------------------------------------------------------
// deals with private API methods:
std::string KAPI::private_method(const std::string& method,
				 const KAPI::Input& input) const
{
   // build method URL
   std::string path = "/" + version_ + "/private/" + method;
   std::string method_url = url_ + path;

   curl_easy_setopt(curl_, CURLOPT_URL, method_url.c_str());

   // create a nonce and and postdata
   std::string nonce = create_nonce();
   std::string postdata = "nonce=" + nonce;

   // if 'input' is not empty generate other postdata
   if (!input.empty())
      postdata = postdata + "&" + build_query(input);
   curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, postdata.c_str());

   // add custom header
   curl_slist* chunk = NULL;

   std::string key_header =  "API-Key: "  + key_;
   std::string sign_header = "API-Sign: " + signature(path, nonce, postdata);

   chunk = curl_slist_append(chunk, key_header.c_str());
   chunk = curl_slist_append(chunk, sign_header.c_str());
   curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, chunk);

   // where CURL write callback function stores the response
   std::string response;
   curl_easy_setopt(curl_, CURLOPT_WRITEDATA, static_cast<void*>(&response));

   // perform CURL request
   CURLcode result = curl_easy_perform(curl_);

   // free the custom headers
   curl_slist_free_all(chunk);

   // check perform result
   if (result != CURLE_OK) {
      std::ostringstream oss;
      oss << "curl_easy_perform() failed: " << curl_easy_strerror(result);
      throw std::runtime_error(oss.str());
   }

   return response;
}

std::string KAPI::private_method(const std::string& method) const
{
   KAPI::Input input;
   return private_method(method, input);
}

//------------------------------------------------------------------------------
// helper function to initialize Kraken API library's resources:
void initialize()
{
   CURLcode code = curl_global_init(CURL_GLOBAL_ALL);
   if (code != CURLE_OK) {
      std::ostringstream oss;
      oss << "curl_global_init() failed: " << curl_easy_strerror(code);
      throw std::runtime_error(oss.str());
   }
}

//------------------------------------------------------------------------------
// helper function to terminate Kraken API library's resources:
void terminate()
{
   curl_global_cleanup();
}

//------------------------------------------------------------------------------

} //namespace Kraken

//------------------------------------------------------------------------------
