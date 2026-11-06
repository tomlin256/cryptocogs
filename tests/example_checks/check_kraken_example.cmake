# check_kraken_example.cmake — test_kraken_example: the WebSocket frames that kraken_example prints (plan 039, #50 and #52).
#
# Run by ctest as `cmake -DEXAMPLE=<kraken_example> -DGOLDEN=<kraken_example_ws_frames.txt> -P check_kraken_example.cmake`.
# The demo's transport is a stub: ws_send prints each frame as "[WS SEND] <json>" and sends nothing, so what the demo builds is
# only visible in what it prints. Nobody read it: the add_order frame left out the order and every private frame left out the
# token, and nothing noticed because nothing ran the program. This runs it and fails unless:
#
#   - it exits 0 (stderr is kept apart, so a sanitizer's report is never read as a frame);
#   - the frames it prints, "[WS SEND] " removed, are the lines of GOLDEN: the same count, in the same order, frame for frame;
#   - no frame has an empty token ("token":""). GOLDEN is the record of today's frames and can be regenerated without being read;
#     this rule is #50 and #52 and does not depend on it.
#
# The REST lines the demo prints carry a nonce, so they are not read. GOLDEN must hold at least one frame, so that an empty file
# cannot make an empty run pass.
#
# To accept a deliberate change to the demo, regenerate GOLDEN and read the diff:
#   <tree>/bin/kraken_example | sed -n 's/^\[WS SEND] //p' > tests/example_checks/kraken_example_ws_frames.txt
#
# The output is split into lines with string(FIND) and not as a CMake list: a frame has [ and ] in it.

cmake_minimum_required(VERSION 4.0)

if(NOT DEFINED EXAMPLE OR NOT DEFINED GOLDEN)
    message(FATAL_ERROR "usage: cmake -DEXAMPLE=<kraken_example> -DGOLDEN=<frames file> -P check_kraken_example.cmake")
endif()

# Splits ${text} into lines, in the caller's scope: ${prefix}_COUNT and ${prefix}_0 ... ${prefix}_<COUNT - 1>. A last line with
# no newline counts; the empty text after the last newline does not.
function(split_lines text prefix)
    set(count 0)
    set(rest "${text}")
    while(NOT "${rest}" STREQUAL "")
        string(FIND "${rest}" "\n" eol)
        if(eol EQUAL -1)
            set(line "${rest}")
            set(rest "")
        else()
            string(SUBSTRING "${rest}" 0 ${eol} line)
            math(EXPR after "${eol} + 1")
            string(SUBSTRING "${rest}" ${after} -1 rest)
        endif()
        set(${prefix}_${count} "${line}" PARENT_SCOPE)
        math(EXPR count "${count} + 1")
    endwhile()
    set(${prefix}_COUNT ${count} PARENT_SCOPE)
endfunction()

execute_process(
    COMMAND "${EXAMPLE}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE printed
    ERROR_VARIABLE complaint)
if(NOT "${status}" STREQUAL "0")
    message(FATAL_ERROR "${EXAMPLE} did not run to the end (status: ${status}). Has the tree been built?\n${complaint}")
endif()

# The frames the demo printed: the lines that start with the prefix, without it.
split_lines("${printed}" LINE)
set(printed_count 0)
set(i 0)
while(i LESS ${LINE_COUNT})
    if("${LINE_${i}}" MATCHES "^\\[WS SEND\\] (.*)$")
        set(PRINTED_${printed_count} "${CMAKE_MATCH_1}")
        math(EXPR printed_count "${printed_count} + 1")
    endif()
    math(EXPR i "${i} + 1")
endwhile()

# The frames it must print.
file(READ "${GOLDEN}" golden_text)
split_lines("${golden_text}" WANT)
if(WANT_COUNT EQUAL 0)
    message(FATAL_ERROR "${GOLDEN} holds no frame: an empty file holds the demo to nothing.")
endif()

if(printed_count GREATER WANT_COUNT)
    set(most ${printed_count})
else()
    set(most ${WANT_COUNT})
endif()

set(problems "")
set(i 0)
while(i LESS most)
    set(want "(no such frame)")
    if(i LESS WANT_COUNT)
        set(want "${WANT_${i}}")
    endif()
    set(got "(no such frame)")
    if(i LESS printed_count)
        set(got "${PRINTED_${i}}")
    endif()
    if(NOT "${got}" STREQUAL "${want}")
        string(APPEND problems "frame ${i}:\n  expected ${want}\n  printed  ${got}\n")
    endif()
    string(FIND "${got}" "\"token\":\"\"" empty_token)
    if(NOT empty_token EQUAL -1)
        string(APPEND problems "frame ${i} has an empty token:\n  ${got}\n")
    endif()
    math(EXPR i "${i} + 1")
endwhile()

if(NOT "${problems}" STREQUAL "")
    message(FATAL_ERROR
        "the WebSocket frames of ${EXAMPLE} are not those of ${GOLDEN} "
        "(${printed_count} printed, ${WANT_COUNT} expected), or one has an empty token:\n${problems}")
endif()
message(STATUS "${printed_count} frames, as in ${GOLDEN}, none with an empty token")
