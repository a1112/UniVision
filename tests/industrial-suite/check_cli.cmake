if(NOT DEFINED CLI OR NOT DEFINED WORKSPACE OR NOT DEFINED FLOW)
  message(FATAL_ERROR "CLI, WORKSPACE and FLOW are required")
endif()
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef suffix)
set(run_root "${WORKSPACE}/demo-${suffix}")
execute_process(
  COMMAND "${CLI}" demo "${run_root}"
  RESULT_VARIABLE code
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(NOT code EQUAL 0)
  message(FATAL_ERROR "CLI demo failed (${code}): ${output}\n${error}")
endif()
set(session_manifest "${run_root}/demo-session/session.json")
set(run_manifest "${run_root}/synthetic-results/demo-run.json")
if(NOT EXISTS "${session_manifest}" OR NOT EXISTS "${run_manifest}")
  message(FATAL_ERROR "CLI did not publish both manifests")
endif()
file(READ "${session_manifest}" session)
file(READ "${run_manifest}" run)
string(JSON complete GET "${session}" complete)
string(JSON frame_count GET "${session}" frame_count)
string(JSON decision GET "${run}" decision)
string(JSON state GET "${run}" state)
if(NOT complete OR NOT frame_count STREQUAL "1" OR
   NOT decision STREQUAL "not-evaluated" OR NOT state STREQUAL "succeeded")
  message(FATAL_ERROR "CLI manifests have wrong state")
endif()
execute_process(
  COMMAND "${CLI}" run-synthetic-flow "${run_root}" "${FLOW}" imported-run
  RESULT_VARIABLE import_code
  OUTPUT_VARIABLE import_output
  ERROR_VARIABLE import_error)
if(NOT import_code EQUAL 0)
  message(FATAL_ERROR "CLI flow import failed (${import_code}): ${import_output}\n${import_error}")
endif()
set(imported_manifest "${run_root}/synthetic-results/imported-run.json")
if(NOT EXISTS "${imported_manifest}")
  message(FATAL_ERROR "CLI did not archive imported flow")
endif()
file(READ "${imported_manifest}" imported)
string(JSON imported_decision GET "${imported}" decision)
if(NOT imported_decision STREQUAL "not-evaluated")
  message(FATAL_ERROR "Imported synthetic flow has wrong decision")
endif()
message(STATUS "CLI demo succeeded with recording and synthetic run")
