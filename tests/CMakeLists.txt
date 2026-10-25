find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(tst_jsonrpc tst_jsonrpc.cpp)
target_link_libraries(tst_jsonrpc PRIVATE qjsonrpc Qt6::Test)
set_property(TARGET tst_jsonrpc PROPERTY CXX_STANDARD 20)

add_test(NAME tst_jsonrpc COMMAND tst_jsonrpc)
