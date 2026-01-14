// This file is based on https://github.com/jean-dao/zig-bgfx.
// Modified to integrate with Floe.
// Copyright 2026 Sam Windell
//
// Copyright (c) 2025 Jean Dao
// SPDX-License-Identifier: MIT

const std = @import("std");

pub fn configBgfxShaderc(exe: *std.Build.Step.Compile, config: struct {
    bgfx: *std.Build.Dependency,
    bimg: *std.Build.Dependency,
}) void {
    // spirv-tools
    exe.addIncludePath(config.bgfx.path("3rdparty/spirv-tools"));
    exe.addIncludePath(config.bgfx.path("3rdparty/spirv-tools/include"));
    exe.addIncludePath(config.bgfx.path("3rdparty/spirv-tools/include/generated"));
    exe.addIncludePath(config.bgfx.path("3rdparty/spirv-tools/source"));
    exe.addIncludePath(config.bgfx.path("3rdparty/spirv-headers/include"));
    exe.addCSourceFiles(.{
        .root = config.bgfx.path("3rdparty/spirv-tools/source"),
        .files = spirv_tools_src_files,
        .flags = spirv_tools_cpp_flags,
    });

    // spirv-cross
    exe.root_module.addCMacro("SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS", "");
    exe.addIncludePath(config.bgfx.path("3rdparty/spirv-cross/include"));
    exe.addCSourceFiles(.{
        .root = config.bgfx.path("3rdparty/spirv-cross"),
        .files = spirv_cross_src_files,
        .flags = spirv_cross_cpp_flags,
    });

    // glslang
    exe.root_module.addCMacro("ENABLE_OPT", "1");
    exe.root_module.addCMacro("ENABLE_HLSL", "1");
    exe.addIncludePath(config.bgfx.path("3rdparty/glslang"));
    exe.addIncludePath(config.bgfx.path("3rdparty"));
    exe.addIncludePath(config.bgfx.path("3rdparty/spirv-tools/include"));
    exe.addIncludePath(config.bgfx.path("3rdparty/spirv-tools/source"));
    exe.addCSourceFiles(.{
        .root = config.bgfx.path("3rdparty/glslang"),
        .files = glslang_src_files,
        .flags = glslang_cpp_flags,
    });

    if (exe.rootModuleTarget().os.tag == .windows) {
        exe.addCSourceFiles(.{
            .root = config.bgfx.path("3rdparty/glslang"),
            .files = windows_glslang_src_files,
            .flags = glslang_cpp_flags,
        });
    } else {
        exe.addCSourceFiles(.{
            .root = config.bgfx.path("3rdparty/glslang"),
            .files = not_windows_glslang_src_files,
            .flags = glslang_cpp_flags,
        });
    }

    // glsl-optimizer
    exe.addIncludePath(config.bgfx.path("3rdparty/glsl-optimizer/include"));
    exe.addIncludePath(config.bgfx.path("3rdparty/glsl-optimizer/src"));
    exe.addIncludePath(config.bgfx.path("3rdparty/glsl-optimizer/src/mesa"));
    exe.addIncludePath(config.bgfx.path("3rdparty/glsl-optimizer/src/mapi"));
    exe.addIncludePath(config.bgfx.path("3rdparty/glsl-optimizer/src/glsl"));
    exe.addCSourceFiles(.{
        .root = config.bgfx.path("3rdparty/glsl-optimizer/src"),
        .files = glsl_optimizer_src_files,
        .flags = glsl_optimizer_cpp_flags,
    });

    // fcpp
    exe.root_module.addCMacro("NINCLUDE", "64");
    exe.root_module.addCMacro("NWORK", "65536");
    exe.root_module.addCMacro("NBUF", "65536");
    exe.root_module.addCMacro("OLD_PREPROCESSOR", "0");
    exe.addCSourceFiles(.{
        .root = config.bgfx.path("3rdparty/fcpp"),
        .files = fcpp_src_files,
        .flags = fcpp_cpp_flags,
    });

    // shaderc
    exe.addIncludePath(config.bimg.path("include"));
    exe.addIncludePath(config.bgfx.path("include"));
    exe.addIncludePath(config.bgfx.path("3rdparty/dxsdk/include"));
    exe.addIncludePath(config.bgfx.path("3rdparty/fcpp"));
    exe.addIncludePath(config.bgfx.path("3rdparty/glslang/glslang/Public"));
    exe.addIncludePath(config.bgfx.path("3rdparty/glslang/glslang/Include"));
    exe.addIncludePath(config.bgfx.path("3rdparty/glslang"));
    exe.addIncludePath(config.bgfx.path("3rdparty/glsl-optimizer/include"));
    exe.addIncludePath(config.bgfx.path("3rdparty/glsl-optimizer/src/glsl"));
    exe.addIncludePath(config.bgfx.path("3rdparty/spirv-cross"));
    exe.addIncludePath(config.bgfx.path("3rdparty/spirv-tools/include"));
    exe.addCSourceFiles(.{
        .root = config.bgfx.path(""),
        .files = shaderc_src_files,
        .flags = shaderc_cpp_flags,
    });

    if (exe.rootModuleTarget().os.tag == .macos) {
        exe.linkFramework("Cocoa");
    }

    exe.root_module.addCMacro("BX_CONFIG_DEBUG", "0");
}

const spirv_tools_cpp_flags = &.{
    "-Wno-switch",
    "-Wno-misleading-indentation",
    "-fno-sanitize=undefined",
};

const spirv_tools_src_files = &.{
    "opt/aggressive_dead_code_elim_pass.cpp",
    "opt/pass_manager.cpp",
    "opt/dead_insert_elim_pass.cpp",
    "opt/remove_unused_interface_variables_pass.cpp",
    "opt/propagator.cpp",
    "opt/eliminate_dead_members_pass.cpp",
    "opt/block_merge_pass.cpp",
    "opt/local_single_store_elim_pass.cpp",
    "opt/loop_fusion_pass.cpp",
    "opt/eliminate_dead_io_components_pass.cpp",
    "opt/build_module.cpp",
    "opt/replace_desc_array_access_using_var_index.cpp",
    "opt/compact_ids_pass.cpp",
    "opt/relax_float_ops_pass.cpp",
    "opt/desc_sroa_util.cpp",
    "opt/instruction.cpp",
    "opt/eliminate_dead_functions_util.cpp",
    "opt/def_use_manager.cpp",
    "opt/optimizer.cpp",
    "opt/ir_context.cpp",
    "opt/modify_maximal_reconvergence.cpp",
    "opt/convert_to_half_pass.cpp",
    "opt/basic_block.cpp",
    "opt/copy_prop_arrays.cpp",
    "opt/replace_invalid_opc.cpp",
    "opt/spread_volatile_semantics.cpp",
    "opt/type_manager.cpp",
    "opt/workaround1209.cpp",
    "opt/module.cpp",
    "opt/loop_dependence.cpp",
    "opt/amd_ext_to_khr.cpp",
    "opt/licm_pass.cpp",
    "opt/types.cpp",
    "opt/wrap_opkill.cpp",
    "opt/register_pressure.cpp",
    "opt/switch_descriptorset_pass.cpp",
    "opt/feature_manager.cpp",
    "opt/combine_access_chains.cpp",
    "opt/pch_source_opt.cpp",
    "opt/mem_pass.cpp",
    "opt/opextinst_forward_ref_fixup_pass.cpp",
    "opt/freeze_spec_constant_value_pass.cpp",
    "opt/scalar_analysis_simplification.cpp",
    "opt/local_single_block_elim_pass.cpp",
    "opt/dead_variable_elimination.cpp",
    "opt/ir_loader.cpp",
    "opt/ssa_rewrite_pass.cpp",
    "opt/scalar_analysis.cpp",
    "opt/dead_branch_elim_pass.cpp",
    "opt/desc_sroa.cpp",
    "opt/fold_spec_constant_op_and_composite_pass.cpp",
    "opt/loop_fission.cpp",
    "opt/vector_dce.cpp",
    "opt/fix_func_call_arguments.cpp",
    "opt/if_conversion.cpp",
    "opt/unify_const_pass.cpp",
    "opt/analyze_live_input_pass.cpp",
    "opt/graphics_robust_access_pass.cpp",
    "opt/struct_cfg_analysis.cpp",
    "opt/inline_exhaustive_pass.cpp",
    "opt/scalar_replacement_pass.cpp",
    "opt/ccp_pass.cpp",
    "opt/private_to_local_pass.cpp",
    "opt/trim_capabilities_pass.cpp",
    "opt/remove_duplicates_pass.cpp",
    "opt/loop_peeling.cpp",
    "opt/loop_unroller.cpp",
    "opt/loop_descriptor.cpp",
    "opt/loop_fusion.cpp",
    "opt/dataflow.cpp",
    "opt/flatten_decoration_pass.cpp",
    "opt/constants.cpp",
    "opt/folding_rules.cpp",
    "opt/eliminate_dead_constant_pass.cpp",
    "opt/simplification_pass.cpp",
    "opt/interp_fixup_pass.cpp",
    "opt/cfg.cpp",
    "opt/local_redundancy_elimination.cpp",
    "opt/reduce_load_size.cpp",
    "opt/strip_debug_info_pass.cpp",
    "opt/code_sink.cpp",
    "opt/loop_dependence_helpers.cpp",
    "opt/set_spec_constant_default_value_pass.cpp",
    "opt/loop_unswitch_pass.cpp",
    "opt/loop_utils.cpp",
    "opt/inline_opaque_pass.cpp",
    "opt/redundancy_elimination.cpp",
    "opt/decoration_manager.cpp",
    "opt/split_combined_image_sampler_pass.cpp",
    "opt/inline_pass.cpp",
    "opt/invocation_interlock_placement_pass.cpp",
    "opt/merge_return_pass.cpp",
    "opt/block_merge_util.cpp",
    "opt/eliminate_dead_output_stores_pass.cpp",
    "opt/fold.cpp",
    "opt/local_access_chain_convert_pass.cpp",
    "opt/remove_dontinline_pass.cpp",
    "opt/cfg_cleanup_pass.cpp",
    "opt/strength_reduction_pass.cpp",
    "opt/pass.cpp",
    "opt/function.cpp",
    "opt/composite.cpp",
    "opt/eliminate_dead_functions_pass.cpp",
    "val/validate_invalid_type.cpp",
    "val/validate_logical_pointers.cpp",
    "opt/graph.cpp",
    "opt/instruction_list.cpp",
    "opt/dominator_analysis.cpp",
    "opt/value_number_table.cpp",
    "opt/control_dependence.cpp",
    "opt/struct_packing_pass.cpp",
    "opt/interface_var_sroa.cpp",
    "opt/liveness.cpp",
    "opt/convert_to_sampled_image_pass.cpp",
    "opt/canonicalize_ids_pass.cpp",
    "opt/dominator_tree.cpp",
    "opt/debug_info_manager.cpp",
    "opt/resolve_binding_conflicts_pass.cpp",
    "opt/const_folding_rules.cpp",
    "opt/strip_nonsemantic_info_pass.cpp",
    "opt/fix_storage_class.cpp",
    "opt/upgrade_memory_model.cpp",
    "reduce/structured_loop_to_selection_reduction_opportunity_finder.cpp",
    "reduce/simple_conditional_branch_to_branch_reduction_opportunity.cpp",
    "reduce/remove_instruction_reduction_opportunity.cpp",
    "reduce/conditional_branch_to_simple_conditional_branch_reduction_opportunity.cpp",
    "reduce/remove_selection_reduction_opportunity.cpp",
    "reduce/reduction_opportunity.cpp",
    "reduce/structured_loop_to_selection_reduction_opportunity.cpp",
    "reduce/structured_construct_to_block_reduction_opportunity_finder.cpp",
    "reduce/remove_selection_reduction_opportunity_finder.cpp",
    "reduce/remove_function_reduction_opportunity_finder.cpp",
    "reduce/reduction_util.cpp",
    "reduce/remove_unused_struct_member_reduction_opportunity_finder.cpp",
    "reduce/pch_source_reduce.cpp",
    "reduce/merge_blocks_reduction_opportunity.cpp",
    "reduce/operand_to_dominating_id_reduction_opportunity_finder.cpp",
    "reduce/operand_to_const_reduction_opportunity_finder.cpp",
    "reduce/conditional_branch_to_simple_conditional_branch_opportunity_finder.cpp",
    "reduce/change_operand_to_undef_reduction_opportunity.cpp",
    "reduce/remove_function_reduction_opportunity.cpp",
    "reduce/reducer.cpp",
    "reduce/structured_construct_to_block_reduction_opportunity.cpp",
    "reduce/reduction_pass.cpp",
    "reduce/merge_blocks_reduction_opportunity_finder.cpp",
    "reduce/operand_to_undef_reduction_opportunity_finder.cpp",
    "reduce/remove_block_reduction_opportunity_finder.cpp",
    "reduce/remove_block_reduction_opportunity.cpp",
    "reduce/change_operand_reduction_opportunity.cpp",
    "reduce/remove_struct_member_reduction_opportunity.cpp",
    "reduce/reduction_opportunity_finder.cpp",
    "reduce/simple_conditional_branch_to_branch_opportunity_finder.cpp",
    "reduce/remove_unused_instruction_reduction_opportunity_finder.cpp",
    "val/construct.cpp",
    "val/validate_logicals.cpp",
    "val/validate_memory_semantics.cpp",
    "val/validate_barriers.cpp",
    "val/validate_decorations.cpp",
    "val/validate_interfaces.cpp",
    "val/validate_image.cpp",
    "val/validate_scopes.cpp",
    "val/instruction.cpp",
    "val/validate_ray_query.cpp",
    "val/basic_block.cpp",
    "val/validate.cpp",
    "val/validate_small_type_uses.cpp",
    "val/validate_instruction.cpp",
    "val/validate_literals.cpp",
    "val/validate_primitives.cpp",
    "val/validate_non_uniform.cpp",
    "val/validate_tensor.cpp",
    "val/validate_graph.cpp",
    "val/validate_composites.cpp",
    "val/validate_tensor_layout.cpp",
    "val/validate_bitwise.cpp",
    "val/validate_capability.cpp",
    "val/validate_function.cpp",
    "val/validate_mesh_shading.cpp",
    "val/validation_state.cpp",
    "val/validate_ray_tracing.cpp",
    "val/validate_misc.cpp",
    "val/validate_cfg.cpp",
    "val/validate_annotation.cpp",
    "val/validate_arithmetics.cpp",
    "val/validate_constants.cpp",
    "val/validate_execution_limitations.cpp",
    "val/validate_builtins.cpp",
    "val/validate_derivatives.cpp",
    "val/validate_extensions.cpp",
    "val/validate_ray_tracing_reorder.cpp",
    "val/validate_type.cpp",
    "val/validate_debug.cpp",
    "val/validate_mode_setting.cpp",
    "val/validate_layout.cpp",
    "val/validate_atomics.cpp",
    "val/function.cpp",
    "val/validate_id.cpp",
    "val/validate_conversion.cpp",
    "val/validate_adjacency.cpp",
    "val/validate_memory.cpp",
    "assembly_grammar.cpp",
    "binary.cpp",
    "diagnostic.cpp",
    "disassemble.cpp",
    "table2.cpp",
    "ext_inst.cpp",
    "extensions.cpp",
    "libspirv.cpp",
    "name_mapper.cpp",
    "opcode.cpp",
    "operand.cpp",
    "parsed_operand.cpp",
    "print.cpp",
    "software_version.cpp",
    "spirv_endian.cpp",
    "spirv_optimizer_options.cpp",
    "spirv_reducer_options.cpp",
    "spirv_target_env.cpp",
    "spirv_validator_options.cpp",
    "table.cpp",
    "text.cpp",
    "text_handler.cpp",
    "to_string.cpp",
    "util/bit_vector.cpp",
    "util/parse_number.cpp",
    "util/string_utils.cpp",
};

const spirv_cross_cpp_flags = &.{
    "-Wno-type-limits",
    "-fno-sanitize=undefined",
};

const spirv_cross_src_files = &.{
    "spirv_cfg.cpp",
    "spirv_cpp.cpp",
    "spirv_cross.cpp",
    "spirv_cross_parsed_ir.cpp",
    "spirv_cross_util.cpp",
    "spirv_glsl.cpp",
    "spirv_hlsl.cpp",
    "spirv_msl.cpp",
    "spirv_parser.cpp",
    "spirv_reflect.cpp",
};

const glslang_cpp_flags = &.{
    "-Wno-logical-op",
    "-Wno-maybe-uninitialized",
    "-fno-strict-aliasing",
    "-Wno-ignored-qualifiers",
    "-Wno-implicit-fallthrough",
    "-Wno-missing-field-initializers",
    "-Wno-reorder",
    "-Wno-return-type",
    "-Wno-shadow",
    "-Wno-sign-compare",
    "-Wno-switch",
    "-Wno-undef",
    "-Wno-unknown-pragmas",
    "-Wno-unused-function",
    "-Wno-unused-parameter",
    "-Wno-unused-variable",
    "-Wno-c++11-extensions",
    "-Wno-unused-const-variable",
    "-Wno-deprecated-register",
    "-Wno-unused-but-set-variable",
    "-fno-sanitize=undefined",
};

const glslang_src_files = &.{
    "glslang/GenericCodeGen/CodeGen.cpp",
    "glslang/GenericCodeGen/Link.cpp",
    "glslang/HLSL/hlslGrammar.cpp",
    "glslang/HLSL/hlslTokenStream.cpp",
    "glslang/HLSL/hlslOpMap.cpp",
    "glslang/HLSL/hlslScanContext.cpp",
    "glslang/HLSL/hlslAttributes.cpp",
    "glslang/HLSL/hlslParseables.cpp",
    "glslang/HLSL/hlslParseHelper.cpp",
    "glslang/CInterface/glslang_c_interface.cpp",
    "glslang/stub.cpp",
    "glslang/MachineIndependent/attribute.cpp",
    "glslang/MachineIndependent/Versions.cpp",
    "glslang/MachineIndependent/ParseHelper.cpp",
    "glslang/MachineIndependent/InfoSink.cpp",
    "glslang/MachineIndependent/linkValidate.cpp",
    "glslang/MachineIndependent/preprocessor/PpContext.cpp",
    "glslang/MachineIndependent/preprocessor/Pp.cpp",
    "glslang/MachineIndependent/preprocessor/PpTokens.cpp",
    "glslang/MachineIndependent/preprocessor/PpScanner.cpp",
    "glslang/MachineIndependent/preprocessor/PpAtom.cpp",
    "glslang/MachineIndependent/IntermTraverse.cpp",
    "glslang/MachineIndependent/limits.cpp",
    "glslang/MachineIndependent/glslang_tab.cpp",
    "glslang/MachineIndependent/parseConst.cpp",
    "glslang/MachineIndependent/Constant.cpp",
    "glslang/MachineIndependent/SpirvIntrinsics.cpp",
    "glslang/MachineIndependent/ShaderLang.cpp",
    "glslang/MachineIndependent/Scan.cpp",
    "glslang/MachineIndependent/ParseContextBase.cpp",
    "glslang/MachineIndependent/intermOut.cpp",
    "glslang/MachineIndependent/reflection.cpp",
    "glslang/MachineIndependent/iomapper.cpp",
    "glslang/MachineIndependent/RemoveTree.cpp",
    "glslang/MachineIndependent/SymbolTable.cpp",
    "glslang/MachineIndependent/Initialize.cpp",
    "glslang/MachineIndependent/PoolAlloc.cpp",
    "glslang/MachineIndependent/Intermediate.cpp",
    "glslang/MachineIndependent/propagateNoContraction.cpp",
    "glslang/ResourceLimits/ResourceLimits.cpp",
    "glslang/ResourceLimits/resource_limits_c.cpp",

    "SPIRV/Logger.cpp",
    "SPIRV/GlslangToSpv.cpp",
    "SPIRV/InReadableOrder.cpp",
    "SPIRV/CInterface/spirv_c_interface.cpp",
    // "SPIRV/SPVRemapper.cpp",
    "SPIRV/SpvBuilder.cpp",
    "SPIRV/doc.cpp",
    "SPIRV/SpvTools.cpp",
    "SPIRV/SpvPostProcess.cpp",
    "SPIRV/disassemble.cpp",
};

const windows_glslang_src_files = &.{
    "glslang/OSDependent/Windows/ossource.cpp",
};

const not_windows_glslang_src_files = &.{
    "glslang/OSDependent/Unix/ossource.cpp",
};

const glsl_optimizer_cpp_flags = &.{
    "-fno-strict-aliasing",
    "-Wno-implicit-fallthrough",
    "-Wno-parentheses",
    "-Wno-sign-compare",
    "-Wno-unused-function",
    "-Wno-unused-parameter",
    "-Wno-deprecated-register",
    "-Wno-misleading-indentation",
    "-fno-sanitize=undefined",
};

const glsl_optimizer_src_files = &.{
    "glsl/glcpp/glcpp-lex.c",
    "glsl/glcpp/glcpp-parse.c",
    "glsl/glcpp/pp.c",
    "glsl/ast_array_index.cpp",
    "glsl/ast_expr.cpp",
    "glsl/ast_function.cpp",
    "glsl/ast_to_hir.cpp",
    "glsl/ast_type.cpp",
    "glsl/builtin_functions.cpp",
    "glsl/builtin_types.cpp",
    "glsl/builtin_variables.cpp",
    "glsl/glsl_lexer.cpp",
    "glsl/glsl_optimizer.cpp",
    "glsl/glsl_parser.cpp",
    "glsl/glsl_parser_extras.cpp",
    "glsl/glsl_symbol_table.cpp",
    "glsl/glsl_types.cpp",
    "glsl/hir_field_selection.cpp",
    "glsl/ir.cpp",
    "glsl/ir_basic_block.cpp",
    "glsl/ir_builder.cpp",
    "glsl/ir_clone.cpp",
    "glsl/ir_constant_expression.cpp",
    "glsl/ir_equals.cpp",
    "glsl/ir_expression_flattening.cpp",
    "glsl/ir_function.cpp",
    "glsl/ir_function_can_inline.cpp",
    "glsl/ir_function_detect_recursion.cpp",
    "glsl/ir_hierarchical_visitor.cpp",
    "glsl/ir_hv_accept.cpp",
    "glsl/ir_import_prototypes.cpp",
    "glsl/ir_print_glsl_visitor.cpp",
    "glsl/ir_print_metal_visitor.cpp",
    "glsl/ir_print_visitor.cpp",
    "glsl/ir_rvalue_visitor.cpp",
    "glsl/ir_stats.cpp",
    "glsl/ir_unused_structs.cpp",
    "glsl/ir_validate.cpp",
    "glsl/ir_variable_refcount.cpp",
    "glsl/link_atomics.cpp",
    "glsl/link_functions.cpp",
    "glsl/link_interface_blocks.cpp",
    "glsl/link_uniform_block_active_visitor.cpp",
    "glsl/link_uniform_blocks.cpp",
    "glsl/link_uniform_initializers.cpp",
    "glsl/link_uniforms.cpp",
    "glsl/link_varyings.cpp",
    "glsl/linker.cpp",
    "glsl/loop_analysis.cpp",
    "glsl/loop_controls.cpp",
    "glsl/loop_unroll.cpp",
    "glsl/lower_clip_distance.cpp",
    "glsl/lower_discard.cpp",
    "glsl/lower_discard_flow.cpp",
    "glsl/lower_if_to_cond_assign.cpp",
    "glsl/lower_instructions.cpp",
    "glsl/lower_jumps.cpp",
    "glsl/lower_mat_op_to_vec.cpp",
    "glsl/lower_named_interface_blocks.cpp",
    "glsl/lower_noise.cpp",
    "glsl/lower_offset_array.cpp",
    "glsl/lower_output_reads.cpp",
    "glsl/lower_packed_varyings.cpp",
    "glsl/lower_packing_builtins.cpp",
    "glsl/lower_ubo_reference.cpp",
    "glsl/lower_variable_index_to_cond_assign.cpp",
    "glsl/lower_vec_index_to_cond_assign.cpp",
    "glsl/lower_vec_index_to_swizzle.cpp",
    "glsl/lower_vector.cpp",
    "glsl/lower_vector_insert.cpp",
    "glsl/lower_vertex_id.cpp",
    "glsl/opt_algebraic.cpp",
    "glsl/opt_array_splitting.cpp",
    "glsl/opt_constant_folding.cpp",
    "glsl/opt_constant_propagation.cpp",
    "glsl/opt_constant_variable.cpp",
    "glsl/opt_copy_propagation.cpp",
    "glsl/opt_copy_propagation_elements.cpp",
    "glsl/opt_cse.cpp",
    "glsl/opt_dead_builtin_variables.cpp",
    "glsl/opt_dead_builtin_varyings.cpp",
    "glsl/opt_dead_code.cpp",
    "glsl/opt_dead_code_local.cpp",
    "glsl/opt_dead_functions.cpp",
    "glsl/opt_flatten_nested_if_blocks.cpp",
    "glsl/opt_flip_matrices.cpp",
    "glsl/opt_function_inlining.cpp",
    "glsl/opt_if_simplification.cpp",
    "glsl/opt_minmax.cpp",
    "glsl/opt_noop_swizzle.cpp",
    "glsl/opt_rebalance_tree.cpp",
    "glsl/opt_redundant_jumps.cpp",
    "glsl/opt_structure_splitting.cpp",
    "glsl/opt_swizzle_swizzle.cpp",
    "glsl/opt_tree_grafting.cpp",
    "glsl/opt_vectorize.cpp",
    "glsl/s_expression.cpp",
    "glsl/standalone_scaffolding.cpp",
    "glsl/strtod.c",
    "mesa/main/imports.c",
    "mesa/program/prog_hash_table.c",
    "mesa/program/symbol_table.c",
    "util/hash_table.c",
    "util/ralloc.c",
};

const fcpp_cpp_flags = &.{
    "-Wno-implicit-fallthrough",
    "-Wno-incompatible-pointer-types",
    "-Wno-parentheses-equality",
    "-fno-sanitize=undefined",
};

const fcpp_src_files = &.{
    "cpp1.c",
    "cpp2.c",
    "cpp3.c",
    "cpp4.c",
    "cpp5.c",
    "cpp6.c",
};

const shaderc_cpp_flags = &.{
    "-fno-rtti",
    "-fno-sanitize=undefined",
    "-std=c++20",
};

const shaderc_src_files = &.{
    "tools/shaderc/shaderc_hlsl.cpp",
    "tools/shaderc/shaderc.cpp",
    "tools/shaderc/shaderc_spirv.cpp",
    "tools/shaderc/shaderc_metal.cpp",
    "tools/shaderc/shaderc_pssl.cpp",
    "tools/shaderc/shaderc_glsl.cpp",
    "src/vertexlayout.cpp",
    "src/shader.cpp",
    "src/shader_dxbc.cpp",
    "src/shader_spirv.cpp",
};
