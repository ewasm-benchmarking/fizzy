// Fizzy: A fast WebAssembly interpreter
// Copyright 2019-2020 The Fizzy Authors.
// SPDX-License-Identifier: Apache-2.0

#include "execute.hpp"
#include "limits.hpp"
#include "parser.hpp"
#include <gtest/gtest.h>
#include <test/utils/asserts.hpp>
#include <test/utils/hex.hpp>

using namespace fizzy;

TEST(execute_call, call)
{
    /* wat2wasm
    (func (result i32) (i32.const 0x2a002a))
    (func (result i32) (call 0))
    */
    const auto wasm =
        from_hex("0061736d010000000105016000017f03030200000a0e02070041aa80a8010b040010000b");
    EXPECT_THAT(execute(parse(wasm), 1, {}), Result(0x2a002a));
}

TEST(execute_call, call_trap)
{
    /* wat2wasm
    (func (result i32) (unreachable))
    (func (result i32) (call 0))
    */
    const auto wasm = from_hex("0061736d010000000105016000017f03030200000a0a020300000b040010000b");

    EXPECT_THAT(execute(parse(wasm), 1, {}), Traps());
}

TEST(execute_call, call_with_arguments)
{
    /* wat2wasm
    (module
      (func $calc (param $a i32) (param $b i32) (result i32)
        local.get 1
        local.get 0
        i32.sub ;; a - b
      )
      (func (result i32)
        i32.const 13
        i32.const 17
        call $calc ;; 17 - 13 => 4
      )
    )
    */
    const auto wasm = from_hex(
        "0061736d01000000010b0260027f7f017f6000017f03030200010a12020700200120006b0b0800410d41111000"
        "0b");

    EXPECT_THAT(execute(parse(wasm), 1, {}), Result(4));
}

TEST(execute_call, call_indirect)
{
    /* wat2wasm
      (type $out-i32 (func (result i32)))

      (table anyfunc (elem $f3 $f2 $f1 $f4 $f5))

      (func $f1 (result i32) i32.const 1)
      (func $f2 (result i32) i32.const 2)
      (func $f3 (result i32) i32.const 3)
      (func $f4 (result i64) i64.const 4)
      (func $f5 (result i32) unreachable)

      (func (param i32) (result i32)
        (call_indirect (type $out-i32) (get_local 0))
      )
    */
    const auto bin = from_hex(
        "0061736d01000000010e036000017f6000017e60017f017f03070600000001000204050170010505090b010041"
        "000b0502010003040a2106040041010b040041020b040041030b040042040b0300000b070020001100000b");

    const Module module = parse(bin);

    for (const auto param : {0u, 1u, 2u})
    {
        constexpr uint64_t expected_results[]{3, 2, 1};

        EXPECT_THAT(execute(module, 5, {param}), Result(expected_results[param]));
    }

    // immediate is incorrect type
    EXPECT_THAT(execute(module, 5, {3}), Traps());

    // called function traps
    EXPECT_THAT(execute(module, 5, {4}), Traps());

    // argument out of table bounds
    EXPECT_THAT(execute(module, 5, {5}), Traps());
}

TEST(execute_call, call_indirect_with_argument)
{
    /* wat2wasm
    (module
      (type $bin_func (func (param i32 i32) (result i32)))
      (table anyfunc (elem $f1 $f2 $f3))

      (func $f1 (param i32 i32) (result i32) (i32.div_u (get_local 0) (get_local 1)))
      (func $f2 (param i32 i32) (result i32) (i32.sub (get_local 0) (get_local 1)))
      (func $f3 (param i32) (result i32) (i32.mul (get_local 0) (get_local 0)))

      (func (param i32) (result i32)
        i32.const 31
        i32.const 7
        (call_indirect (type $bin_func) (get_local 0))
      )
    )
    */
    const auto bin = from_hex(
        "0061736d01000000010c0260027f7f017f60017f017f03050400000101040501700103030909010041000b0300"
        "01020a25040700200020016e0b0700200020016b0b0700200020006c0b0b00411f410720001100000b");

    const Module module = parse(bin);

    EXPECT_THAT(execute(module, 3, {0}), Result(31 / 7));
    EXPECT_THAT(execute(module, 3, {1}), Result(31 - 7));

    // immediate is incorrect type
    EXPECT_THAT(execute(module, 3, {2}), Traps());
}

TEST(execute_call, call_indirect_imported_table)
{
    /* wat2wasm
    (module
      (type $out_i32 (func (result i32)))
      (import "m" "t" (table 5 20 anyfunc))

      (func (param i32) (result i32)
        (call_indirect (type $out_i32) (get_local 0))
      )
    )
    */
    const auto bin = from_hex(
        "0061736d01000000010a026000017f60017f017f020a01016d01740170010514030201010a0901070020001100"
        "000b");

    const Module module = parse(bin);

    auto f1 = [](Instance&, std::vector<uint64_t>, int) { return execution_result{false, {1}}; };
    auto f2 = [](Instance&, std::vector<uint64_t>, int) { return execution_result{false, {2}}; };
    auto f3 = [](Instance&, std::vector<uint64_t>, int) { return execution_result{false, {3}}; };
    auto f4 = [](Instance&, std::vector<uint64_t>, int) { return execution_result{false, {4}}; };
    auto f5 = [](Instance&, std::vector<uint64_t>, int) { return execution_result{true, {}}; };

    auto out_i32 = FuncType{{}, {ValType::i32}};
    auto out_i64 = FuncType{{}, {ValType::i64}};

    table_elements table{
        {{f3, out_i32}}, {{f2, out_i32}}, {{f1, out_i32}}, {{f4, out_i64}}, {{f5, out_i32}}};

    auto instance = instantiate(module, {}, {{&table, {5, 20}}});

    for (const auto param : {0u, 1u, 2u})
    {
        constexpr uint64_t expected_results[]{3, 2, 1};

        EXPECT_THAT(execute(*instance, 0, {param}), Result(expected_results[param]));
    }

    // immediate is incorrect type
    EXPECT_THAT(execute(*instance, 0, {3}), Traps());

    // called function traps
    EXPECT_THAT(execute(*instance, 0, {4}), Traps());

    // argument out of table bounds
    EXPECT_THAT(execute(*instance, 0, {5}), Traps());
}

TEST(execute_call, call_indirect_uninited_table)
{
    /* wat2wasm
      (type $out-i32 (func (result i32)))

      (table 5 anyfunc)
      (elem (i32.const 0) $f3 $f2 $f1)

      (func $f1 (result i32) i32.const 1)
      (func $f2 (result i32) i32.const 2)
      (func $f3 (result i32) i32.const 3)

      (func (param i32) (result i32)
        (call_indirect (type $out-i32) (get_local 0))
      )
    */
    const auto bin = from_hex(
        "0061736d01000000010a026000017f60017f017f030504000000010404017000050909010041000b030201000a"
        "1804040041010b040041020b040041030b070020001100000b");

    const Module module = parse(bin);

    // elements 3 and 4 are not initialized
    EXPECT_THAT(execute(module, 3, {3}), Traps());
    EXPECT_THAT(execute(module, 3, {4}), Traps());
}

TEST(execute_call, imported_function_call)
{
    /* wat2wasm
    (import "mod" "foo" (func (result i32)))
    (func (result i32)
      call 0
    )
    */
    const auto wasm = from_hex(
        "0061736d010000000105016000017f020b01036d6f6403666f6f0000030201000a0601040010000b");

    const auto module = parse(wasm);

    constexpr auto host_foo = [](Instance&, std::vector<uint64_t>, int) -> execution_result {
        return {false, {42}};
    };
    const auto host_foo_type = module.typesec[0];

    auto instance = instantiate(module, {{host_foo, host_foo_type}});

    EXPECT_THAT(execute(*instance, 1, {}), Result(42));
}

TEST(execute_call, imported_function_call_with_arguments)
{
    /* wat2wasm
    (import "mod" "foo" (func (param i32) (result i32)))
    (func (param i32) (result i32)
      get_local 0
      call 0
      i32.const 2
      i32.add
    )
    */
    const auto wasm = from_hex(
        "0061736d0100000001060160017f017f020b01036d6f6403666f6f0000030201000a0b0109002000100041026a"
        "0b");

    const auto module = parse(wasm);

    auto host_foo = [](Instance&, std::vector<uint64_t> args, int) -> execution_result {
        return {false, {args[0] * 2}};
    };
    const auto host_foo_type = module.typesec[0];

    auto instance = instantiate(module, {{host_foo, host_foo_type}});

    EXPECT_THAT(execute(*instance, 1, {20}), Result(42));
}

TEST(execute_call, imported_functions_call_indirect)
{
    /* wat2wasm
    (module
      (type $ft (func (param i32) (result i64)))
      (func $sqr    (import "env" "sqr") (param i32) (result i64))
      (func $isqrt  (import "env" "isqrt") (param i32) (result i64))
      (func $double (param i32) (result i64)
        get_local 0
        i64.extend_u/i32
        get_local 0
        i64.extend_u/i32
        i64.add
      )

      (func $main (param i32) (param i32) (result i64)
        get_local 1
        get_local 0
        call_indirect (type $ft)
      )

      (table anyfunc (elem $double $sqr $isqrt))
    )
    */
    const auto wasm = from_hex(
        "0061736d01000000010c0260017f017e60027f7f017e02170203656e7603737172000003656e76056973717274"
        "00000303020001040501700103030909010041000b030200010a150209002000ad2000ad7c0b09002001200011"
        "00000b");

    const auto module = parse(wasm);
    ASSERT_EQ(module.typesec.size(), 2);
    ASSERT_EQ(module.importsec.size(), 2);
    ASSERT_EQ(module.codesec.size(), 2);

    constexpr auto sqr = [](Instance&, std::vector<uint64_t> args, int) -> execution_result {
        return {false, {args[0] * args[0]}};
    };
    constexpr auto isqrt = [](Instance&, std::vector<uint64_t> args, int) -> execution_result {
        return {false, {(11 + args[0] / 11) / 2}};
    };

    auto instance = instantiate(module, {{sqr, module.typesec[0]}, {isqrt, module.typesec[0]}});
    EXPECT_THAT(execute(*instance, 3, {0, 10}), Result(20));  // double(10)
    EXPECT_THAT(execute(*instance, 3, {1, 9}), Result(81));   // sqr(9)
    EXPECT_THAT(execute(*instance, 3, {2, 50}), Result(7));   // isqrt(50)
}

TEST(execute_call, imported_function_from_another_module)
{
    /* wat2wasm
    (module
      (func $sub (param $lhs i32) (param $rhs i32) (result i32)
        get_local $lhs
        get_local $rhs
        i32.sub)
      (export "sub" (func $sub))
    )
    */
    const auto bin1 = from_hex(
        "0061736d0100000001070160027f7f017f030201000707010373756200000a09010700200020016b0b");
    const auto module1 = parse(bin1);
    auto instance1 = instantiate(module1);

    /* wat2wasm
    (module
      (func $sub (import "m1" "sub") (param $lhs i32) (param $rhs i32) (result i32))

      (func $main (param i32) (param i32) (result i32)
        get_local 0
        get_local 1
        call $sub
      )
    )
    */
    const auto bin2 = from_hex(
        "0061736d0100000001070160027f7f017f020a01026d31037375620000030201000a0a0108002000200110000"
        "b");
    const auto module2 = parse(bin2);

    const auto func_idx = fizzy::find_exported_function(module1, "sub");
    ASSERT_TRUE(func_idx.has_value());

    auto sub = [&instance1, func_idx](
                   Instance&, std::vector<uint64_t> args, int) -> execution_result {
        return fizzy::execute(*instance1, *func_idx, std::move(args));
    };

    auto instance2 = instantiate(module2, {{sub, module1.typesec[0]}});

    EXPECT_THAT(execute(*instance2, 1, {44, 2}), Result(42));
}

TEST(execute_call, imported_table_from_another_module)
{
    /* wat2wasm
    (module
      (func $sub (param $lhs i32) (param $rhs i32) (result i32)
        get_local $lhs
        get_local $rhs
        i32.sub)
      (table (export "tab") 1 funcref)
      (elem (i32.const 0) $sub)
    )
    */
    const auto bin1 = from_hex(
        "0061736d0100000001070160027f7f017f030201000404017000010707010374616201000907010041000b0100"
        "0a09010700200020016b0b");
    const auto module1 = parse(bin1);
    auto instance1 = instantiate(module1);

    /* wat2wasm
    (module
      (type $t1 (func (param $lhs i32) (param $rhs i32) (result i32)))
      (import "m1" "tab" (table 1 funcref))

      (func $main (param i32) (param i32) (result i32)
        get_local 0
        get_local 1
        (call_indirect (type $t1) (i32.const 0))
      )
    )
    */
    const auto bin2 = from_hex(
        "0061736d0100000001070160027f7f017f020c01026d310374616201700001030201000a0d010b002000200141"
        "001100000b");
    const auto module2 = parse(bin2);

    const auto table = fizzy::find_exported_table(*instance1, "tab");
    ASSERT_TRUE(table.has_value());

    auto instance2 = instantiate(module2, {}, {*table});

    EXPECT_THAT(execute(*instance2, 0, {44, 2}), Result(42));
}

TEST(execute_call, imported_table_modified_by_uninstantiable_module)
{
    /* wat2wasm
    (module
      (type $t1 (func (param $lhs i32) (param $rhs i32) (result i32)))
      (func (param i32) (param i32) (result i32)
        get_local 0
        get_local 1
        (call_indirect (type $t1) (i32.const 0))
      )
      (table (export "tab") 1 funcref)
    )
    */
    const auto bin1 = from_hex(
        "0061736d0100000001070160027f7f017f030201000404017000010707010374616201000a0d010b0020002001"
        "41001100000b");
    const auto module1 = parse(bin1);
    auto instance1 = instantiate(module1);

    /* wat2wasm
    (module
      (import "m1" "tab" (table 1 funcref))
      (func $sub (param $lhs i32) (param $rhs i32) (result i32)
        get_local $lhs
        get_local $rhs
        i32.sub)
      (elem (i32.const 0) $sub)
      (func $main (unreachable))
      (start $main)
    )
    */
    const auto bin2 = from_hex(
        "0061736d01000000010a0260027f7f017f600000020c01026d3103746162017000010303020001080101090701"
        "0041000b01000a0d020700200020016b0b0300000b");
    const auto module2 = parse(bin2);

    const auto table = fizzy::find_exported_table(*instance1, "tab");
    ASSERT_TRUE(table.has_value());

    EXPECT_THROW_MESSAGE(
        instantiate(module2, {}, {*table}), instantiate_error, "start function failed to execute");

    EXPECT_THAT(execute(*instance1, 0, {44, 2}), Result(42));
}

TEST(execute_call, call_infinite_recursion)
{
    /* wat2wasm
    (module (func call 0))
    */
    const auto bin = from_hex("0061736d01000000010401600000030201000a0601040010000b");

    const Module module = parse(bin);

    EXPECT_THAT(execute(module, 0, {}), Traps());
}

TEST(execute_call, call_indirect_infinite_recursion)
{
    /* wat2wasm
      (type $out-i32 (func (result i32)))
      (table anyfunc (elem $foo))
      (func $foo (result i32)
        (call_indirect (type $out-i32) (i32.const 0))
      )
    */
    const auto bin = from_hex(
        "0061736d010000000105016000017f03020100040501700101010907010041000b01000a090107004100110000"
        "0b");

    const Module module = parse(bin);

    EXPECT_TRUE(execute(module, 0, {}).trapped);
}

TEST(execute_call, call_max_depth)
{
    /* wat2wasm
    (func (result i32) (i32.const 42))
    (func (result i32) (call 0))
    */

    const auto bin = from_hex("0061736d010000000105016000017f03030200000a0b020400412a0b040010000b");

    const auto module = parse(bin);
    auto instance = instantiate(module);

    EXPECT_THAT(execute(*instance, 0, {}, 2048), Result(42));
    EXPECT_THAT(execute(*instance, 1, {}, 2048), Traps());
}

// A regression test for incorrect number of arguments passed to a call.
TEST(execute_call, call_nonempty_stack)
{
    /* wat2wasm
    (func (param i32) (result i32)
      local.get 0
    )
    (func (result i32)
      i32.const 1
      i32.const 2
      call 0
      i32.add
    )
    */

    const auto wasm = from_hex(
        "0061736d01000000010a0260017f017f6000017f03030200010a1002040020000b09004101410210006a0b");

    auto instance = instantiate(parse(wasm));

    EXPECT_THAT(execute(*instance, 1, {}), Result(3));
}

TEST(execute_call, call_imported_infinite_recursion)
{
    /* wat2wasm
    (import "mod" "foo" (func (result i32)))
    (func (result i32)
      call 0
    )
    */
    const auto wasm = from_hex(
        "0061736d010000000105016000017f020b01036d6f6403666f6f0000030201000a0601040010000b");

    const auto module = parse(wasm);
    auto host_foo = [](Instance& instance, std::vector<uint64_t>, int depth) -> execution_result {
        return execute(instance, 0, {}, depth + 1);
    };
    const auto host_foo_type = module.typesec[0];

    auto instance = instantiate(module, {{host_foo, host_foo_type}});

    EXPECT_THAT(execute(*instance, 0, {}), Traps());
}

TEST(execute_call, call_indirect_imported_table_infinite_recursion)
{
    /* wat2wasm
    (module
      (type (func (result i32)))
      (table (export "tab") 2 funcref)
      (elem (i32.const 0) $f1)
      (func $f1 (result i32)
        (call_indirect (type 0) (i32.const 1))
      )
    )
    */
    const auto bin1 = from_hex(
        "0061736d010000000105016000017f030201000404017000020707010374616201000907010041000b01000a09"
        "01070041011100000b");
    const auto module1 = parse(bin1);
    auto instance1 = instantiate(module1);

    /* wat2wasm
    (module
      (type (func (result i32)))
      (import "m1" "tab" (table 1 funcref))
      (elem (i32.const 1) $f2)
      (func $f2 (result i32)
        (call_indirect (type 0) (i32.const 0))
      )
    )
    */
    const auto bin2 = from_hex(
        "0061736d010000000105016000017f020c01026d310374616201700001030201000907010041010b01000a0901"
        "070041001100000b");
    const auto module2 = parse(bin2);

    const auto table = fizzy::find_exported_table(*instance1, "tab");
    ASSERT_TRUE(table.has_value());

    auto instance2 = instantiate(module2, {}, {*table});

    EXPECT_THAT(execute(*instance1, 0, {}), Traps());
}

TEST(execute_call, drop_call_result)
{
    // Regression test for incorrect max_stack_height based on call.wast:287.
    /* wat2wasm
      (func $const-i32 (result i32) (i32.const 0x132))
      (func (export "drop_call_result")
        call $const-i32
        drop
      )
    */
    const auto wasm = from_hex(
        "0061736d010000000108026000017f60000003030200010714011064726f705f63616c6c5f726573756c740001"
        "0a0d02050041b2020b050010001a0b");

    const auto module = parse(wasm);
    ASSERT_EQ(module.codesec.size(), 2);
    EXPECT_EQ(module.codesec[0].max_stack_height, 1);
    EXPECT_EQ(module.codesec[1].max_stack_height, 1);
    const auto func_idx = find_exported_function(module, "drop_call_result");
    auto instance = instantiate(module);
    EXPECT_THAT(fizzy::execute(*instance, *func_idx, {}), Result());
}
