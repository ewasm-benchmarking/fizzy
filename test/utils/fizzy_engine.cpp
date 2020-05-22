// Fizzy: A fast WebAssembly interpreter
// Copyright 2019-2020 The Fizzy Authors.
// SPDX-License-Identifier: Apache-2.0

#include "execute.hpp"
#include "parser.hpp"

#include <test/utils/adler32.hpp>
#include <test/utils/wasm_engine.hpp>
#include <cassert>
#include <cstring>

#define BIGINT_BITS 384
#define LIMB_BITS 64
#define LIMB_BITS_OVERFLOW 128
#include <test/utils/bigint.h>
#undef BIGINT_BITS
#undef LIMB_BITS
#undef LIMB_BITS_OVERFLOW

namespace fizzy::test
{
class FizzyEngine : public WasmEngine
{
    std::unique_ptr<Instance> m_instance;

public:
    bool parse(bytes_view input) const final;
    std::optional<FuncRef> find_function(
        std::string_view name, std::string_view signature) const final;
    bool instantiate(bytes_view wasm_binary) final;
    bool init_memory(bytes_view memory) final;
    bytes_view get_memory() const final;
    Result execute(FuncRef func_ref, const std::vector<uint64_t>& args) final;
};

namespace
{
ValType translate_valtype(char input)
{
    if (input == 'i')
        return fizzy::ValType::i32;
    else if (input == 'I')
        return fizzy::ValType::i64;
    else
        throw std::runtime_error{"invalid type"};
}

FuncType translate_signature(std::string_view signature)
{
    const auto delimiter_pos = signature.find(":");
    assert(delimiter_pos != std::string_view::npos);
    const auto inputs = signature.substr(0, delimiter_pos);
    const auto outputs = signature.substr(delimiter_pos + 1);

    FuncType func_type;
    std::transform(std::begin(inputs), std::end(inputs), std::back_inserter(func_type.inputs),
        translate_valtype);
    std::transform(std::begin(outputs), std::end(outputs), std::back_inserter(func_type.outputs),
        translate_valtype);
    return func_type;
}

fizzy::execution_result env_adler32(fizzy::Instance& instance, std::vector<uint64_t> args, int)
{
    assert(instance.memory != nullptr);
    const auto ret = fizzy::test::adler32(bytes_view{*instance.memory}.substr(args[0], args[1]));
    return {false, {ret}};
}

const uint64_t mod[] = {0xb9feffffffffaaab, 0x1eabfffeb153ffff, 0x6730d2a0f6b0f624,
    0x64774b84f38512bf, 0x4b1ba7b6434bacd7, 0x1a0111ea397fe69a};
const uint64_t modinv = 0x89f3fffcfffcfffd;

fizzy::execution_result bignum_int_add(fizzy::Instance& instance, std::vector<uint64_t> args, int)
{
    const uint32_t a_offset = static_cast<uint32_t>(args[0]);
    const uint32_t b_offset = static_cast<uint32_t>(args[1]);
    const uint32_t ret_offset = static_cast<uint32_t>(args[2]);
    const uint64_t* a = reinterpret_cast<uint64_t*>(&(instance.memory->data()[a_offset]));
    const uint64_t* b = reinterpret_cast<uint64_t*>(&(instance.memory->data()[b_offset]));
    uint64_t* out = reinterpret_cast<uint64_t*>(&(instance.memory->data()[ret_offset]));
    const auto ret = add384_64bitlimbs(out, a, b);
    return {false, {ret}};
}
fizzy::execution_result bignum_int_sub(fizzy::Instance& instance, std::vector<uint64_t> args, int)
{
    const uint32_t a_offset = static_cast<uint32_t>(args[0]);
    const uint32_t b_offset = static_cast<uint32_t>(args[1]);
    const uint32_t ret_offset = static_cast<uint32_t>(args[2]);
    const uint64_t* a = reinterpret_cast<uint64_t*>(&(instance.memory->data()[a_offset]));
    const uint64_t* b = reinterpret_cast<uint64_t*>(&(instance.memory->data()[b_offset]));
    uint64_t* out = reinterpret_cast<uint64_t*>(&(instance.memory->data()[ret_offset]));
    const auto ret = sub384_64bitlimbs(out, a, b);
    return {false, {ret}};
}
fizzy::execution_result bignum_int_mul(fizzy::Instance& instance, std::vector<uint64_t> args, int)
{
    const uint32_t a_offset = static_cast<uint32_t>(args[0]);
    const uint32_t b_offset = static_cast<uint32_t>(args[1]);
    const uint32_t ret_offset = static_cast<uint32_t>(args[2]);
    const uint64_t* a = reinterpret_cast<uint64_t*>(&(instance.memory->data()[a_offset]));
    const uint64_t* b = reinterpret_cast<uint64_t*>(&(instance.memory->data()[b_offset]));
    uint64_t* out = reinterpret_cast<uint64_t*>(&(instance.memory->data()[ret_offset]));
    mul384_64bitlimbs(out, a, b);
    return {false, {}};
}
fizzy::execution_result bignum_int_div(fizzy::Instance& instance, std::vector<uint64_t> args, int)
{
    const uint32_t a_offset = static_cast<uint32_t>(args[0]);
    const uint32_t b_offset = static_cast<uint32_t>(args[1]);
    const uint32_t q_offset = static_cast<uint32_t>(args[2]);
    const uint32_t r_offset = static_cast<uint32_t>(args[3]);
    const uint64_t* a = reinterpret_cast<uint64_t*>(&(instance.memory->data()[a_offset]));
    const uint64_t* b = reinterpret_cast<uint64_t*>(&(instance.memory->data()[b_offset]));
    uint64_t* q = reinterpret_cast<uint64_t*>(&(instance.memory->data()[q_offset]));
    uint64_t* r = reinterpret_cast<uint64_t*>(&(instance.memory->data()[r_offset]));
    div384_64bitlimbs(q, r, a, b);
    return {false, {}};
}
fizzy::execution_result bignum_f1m_add(fizzy::Instance& instance, std::vector<uint64_t> args, int)
{
    const uint32_t a_offset = static_cast<uint32_t>(args[0]);
    const uint32_t b_offset = static_cast<uint32_t>(args[1]);
    const uint32_t ret_offset = static_cast<uint32_t>(args[2]);
    const uint64_t* a = reinterpret_cast<uint64_t*>(&(instance.memory->data()[a_offset]));
    const uint64_t* b = reinterpret_cast<uint64_t*>(&(instance.memory->data()[b_offset]));
    uint64_t* out = reinterpret_cast<uint64_t*>(&(instance.memory->data()[ret_offset]));
    addmod384_64bitlimbs(out, a, b, mod);
    return {false, {}};
}
fizzy::execution_result bignum_f1m_sub(fizzy::Instance& instance, std::vector<uint64_t> args, int)
{
    const uint32_t a_offset = static_cast<uint32_t>(args[0]);
    const uint32_t b_offset = static_cast<uint32_t>(args[1]);
    const uint32_t ret_offset = static_cast<uint32_t>(args[2]);
    const uint64_t* a = reinterpret_cast<uint64_t*>(&(instance.memory->data()[a_offset]));
    const uint64_t* b = reinterpret_cast<uint64_t*>(&(instance.memory->data()[b_offset]));
    uint64_t* out = reinterpret_cast<uint64_t*>(&(instance.memory->data()[ret_offset]));
    submod384_64bitlimbs(out, a, b, mod);
    return {false, {}};
}
fizzy::execution_result bignum_f1m_mul(fizzy::Instance& instance, std::vector<uint64_t> args, int)
{
    const uint32_t a_offset = static_cast<uint32_t>(args[0]);
    const uint32_t b_offset = static_cast<uint32_t>(args[1]);
    const uint32_t ret_offset = static_cast<uint32_t>(args[2]);
    const uint64_t* a = reinterpret_cast<uint64_t*>(&(instance.memory->data()[a_offset]));
    const uint64_t* b = reinterpret_cast<uint64_t*>(&(instance.memory->data()[b_offset]));
    uint64_t* out = reinterpret_cast<uint64_t*>(&(instance.memory->data()[ret_offset]));
    mulmodmont384_64bitlimbs(out, a, b, mod, modinv);
    return {false, {}};
}
}  // namespace

std::unique_ptr<WasmEngine> create_fizzy_engine()
{
    return std::make_unique<FizzyEngine>();
}

bool FizzyEngine::parse(bytes_view input) const
{
    try
    {
        fizzy::parse(input);
    }
    catch (...)
    {
        return false;
    }
    return true;
}

bool FizzyEngine::instantiate(bytes_view wasm_binary)
{
    try
    {
        const auto module = fizzy::parse(wasm_binary);
        auto imports = fizzy::resolve_imported_functions(
            module, {
                        {"env", "adler32", {fizzy::ValType::i32, fizzy::ValType::i32},
                            fizzy::ValType::i32, env_adler32},
                        {"env", "bignum_int_add",
                            {fizzy::ValType::i32, fizzy::ValType::i32, fizzy::ValType::i32},
                            fizzy::ValType::i32, bignum_int_add},
                        {"env", "bignum_int_sub",
                            {fizzy::ValType::i32, fizzy::ValType::i32, fizzy::ValType::i32},
                            fizzy::ValType::i32, bignum_int_sub},
                        {"env", "bignum_int_mul",
                            {fizzy::ValType::i32, fizzy::ValType::i32, fizzy::ValType::i32},
                            std::nullopt, bignum_int_mul},
                        {"env", "bignum_int_div",
                            {fizzy::ValType::i32, fizzy::ValType::i32, fizzy::ValType::i32,
                                fizzy::ValType::i32},
                            std::nullopt, bignum_int_div},
                        {"env", "bignum_f1m_add",
                            {fizzy::ValType::i32, fizzy::ValType::i32, fizzy::ValType::i32},
                            std::nullopt, bignum_f1m_add},
                        {"env", "bignum_f1m_sub",
                            {fizzy::ValType::i32, fizzy::ValType::i32, fizzy::ValType::i32},
                            std::nullopt, bignum_f1m_sub},
                        {"env", "bignum_f1m_mul",
                            {fizzy::ValType::i32, fizzy::ValType::i32, fizzy::ValType::i32},
                            std::nullopt, bignum_f1m_mul},
                    });
        m_instance = fizzy::instantiate(module, imports);
    }
    catch (...)
    {
        return false;
    }
    assert(m_instance != nullptr);
    return true;
}

bool FizzyEngine::init_memory(bytes_view memory)
{
    if (m_instance->memory == nullptr || m_instance->memory->size() < memory.size())
        return false;

    std::memcpy(m_instance->memory->data(), memory.data(), memory.size());
    return true;
}

bytes_view FizzyEngine::get_memory() const
{
    if (!m_instance->memory)
        return {};

    return {m_instance->memory->data(), m_instance->memory->size()};
}

std::optional<WasmEngine::FuncRef> FizzyEngine::find_function(
    std::string_view name, std::string_view signature) const
{
    const auto func_idx = fizzy::find_exported_function(m_instance->module, name);
    if (func_idx.has_value())
    {
        const auto func_type = m_instance->module.get_function_type(*func_idx);
        const auto sig_type = translate_signature(signature);
        if (sig_type != func_type)
            return std::nullopt;
    }
    return func_idx;
}

WasmEngine::Result FizzyEngine::execute(
    WasmEngine::FuncRef func_ref, const std::vector<uint64_t>& args)
{
    const auto [trapped, result_stack] =
        fizzy::execute(*m_instance, static_cast<uint32_t>(func_ref), args);
    return {trapped, !result_stack.empty() ? result_stack.back() : std::optional<uint64_t>{}};
}
}  // namespace fizzy::test
