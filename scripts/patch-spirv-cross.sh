#!/bin/bash
# Re-applies the fork's SPIRV-Cross MSL emission changes to the MVK's pinned
# External copy (the fetchDependencies checkout wipes them every build).
# Usage: patch-spirv-cross.sh <path-to-spirv_msl.cpp>
set -e
SPIRV_MSL="$1"
if grep -q "BuiltInFullyCoveredEXT" "$SPIRV_MSL"; then
  echo "SPIRV-Cross fork patch already present"
  exit 0
fi
python3 - "$SPIRV_MSL" <<'PATCH'
import sys
p = sys.argv[1]
s = open(p).read()
old = "\tcase BuiltInDeviceIndex:\n\t\treturn \"int\";\n\n\tcase BuiltInPrimitivePointIndicesEXT:"
new = "\tcase BuiltInDeviceIndex:\n\t\treturn \"int\";\n\n\tcase BuiltInFullyCoveredEXT:\n\t\treturn \"bool\";\n\n\tcase BuiltInPrimitivePointIndicesEXT:"
assert s.count(old) == 1, "type decl"
s = s.replace(old, new)
old = "\tcase BuiltInCullPrimitiveEXT:\n\t\treturn \"primitive_culled\";\n\n\tdefault:\n\t\treturn \"unsupported-built-in\";"
new = "\tcase BuiltInCullPrimitiveEXT:\n\t\treturn \"primitive_culled\";\n\n\tcase BuiltInFullyCoveredEXT:\n\t\t// MoltenVK-macOS fork: Metal has no inner-coverage attribute; the MVK's\n\t\t// conservative-rasterization emulation computes the fully-covered bool\n\t\t// in the fragment shader, so emit the argument with no attribute.\n\t\treturn \"\";\n\n\tdefault:\n\t\treturn \"unsupported-built-in\";"
assert s.count(old) == 1, "qualifier"
s = s.replace(old, new)
old = '\t\t\t\tep_args += string(" [[") + builtin_qualifier(bi_type);\n\t\t\t\tif (bi_type == BuiltInSampleMask && get_entry_point().flags.get(ExecutionModePostDepthCoverage))\n\t\t\t\t{\n\t\t\t\t\tif (!msl_options.supports_msl_version(2))\n\t\t\t\t\t\tSPIRV_CROSS_THROW("Post-depth coverage requires MSL 2.0.");\n\t\t\t\t\tif (msl_options.is_macos() && !msl_options.supports_msl_version(2, 3))\n\t\t\t\t\t\tSPIRV_CROSS_THROW("Post-depth coverage on Mac requires MSL 2.3.");\n\t\t\t\t\tep_args += ", post_depth_coverage";\n\t\t\t\t}\n\t\t\t\tep_args += "]]";'
new = '\t\t\t\t{\n\t\t\t\t\tstring mslQual = builtin_qualifier(bi_type);\n\t\t\t\t\tif (!mslQual.empty())\n\t\t\t\t\t{\n\t\t\t\t\t\tep_args += string(" [[") + mslQual;\n\t\t\t\t\t\tif (bi_type == BuiltInSampleMask && get_entry_point().flags.get(ExecutionModePostDepthCoverage))\n\t\t\t\t\t\t{\n\t\t\t\t\t\t\tif (!msl_options.supports_msl_version(2))\n\t\t\t\t\t\t\t\tSPIRV_CROSS_THROW("Post-depth coverage requires MSL 2.0.");\n\t\t\t\t\t\t\tif (msl_options.is_macos() && !msl_options.supports_msl_version(2, 3))\n\t\t\t\t\t\t\t\tSPIRV_CROSS_THROW("Post-depth coverage on Mac requires MSL 2.3.");\n\t\t\t\t\t\t\tep_args += ", post_depth_coverage";\n\t\t\t\t\t\t}\n\t\t\t\t\t\tep_args += "]]";\n\t\t\t\t\t}\n\t\t\t\t}'
assert s.count(old) == 1, "emission"
s = s.replace(old, new)
open(p, "w").write(s)
print("SPIRV-Cross fork patch applied")
PATCH

# The 64-bit image atomic emulation (the sampler-feedback writes): the R64UI
# image pixel type becomes uint (a 2x32 view) and the 64-bit atomic OR is
# emitted as the Metal texel atomic with the full-texel (vec4) operand.
python3 - "$SPIRV_MSL" <<'PATCH2'
import sys
p = sys.argv[1]
s = open(p).read()
if "64-bit feedback images are exposed as 2x32 views" in s and "simd_or((uint)" in s:
    print("int64-image-atomic patch already present")
    sys.exit(0)
# 1. the R64UI image pixel type -> uint (a 2x32 view)
old = "\t// Append the pixel type\n\timg_type_name += \"<\";\n\timg_type_name += type_to_glsl(get<SPIRType>(img_type.type));"
new = ("\t// Append the pixel type\n\timg_type_name += \"<\";\n"
       "\tif (img_type.format == ImageFormatR64ui)\n"
       "\t{\n"
       "\t\t// MVK: the 64-bit feedback images are exposed as 2x32 views; the\n"
       "\t\t// Metal pixel type is uint (the 64-bit atomics are emulated).\n"
       "\t\timg_type_name += \"uint\";\n"
       "\t}\n"
       "\telse\n"
       "\t\timg_type_name += type_to_glsl(get<SPIRType>(img_type.type));")
assert s.count(old) == 1, "img type"
s = s.replace(old, new)
# 2. the 64-bit image atomic OR -> the Metal texel atomic (full-texel vec4)
old = "\tif (type.width == 64)\n\t\tSPIRV_CROSS_THROW(\"MSL currently does not support 64-bit atomics.\");"
new = ("\tif (type.width == 64)\n"
       "\t{\n"
       "\t\t// MVK: the 64-bit image atomics (the sampler-feedback writes) are\n"
       "\t\t// emulated via the Metal texel atomic on the 2x32 view: the two\n"
       "\t\t// 32-bit words are OR'd in a single atomic on the whole texel.\n"
       "\t\tif ((type.storage == StorageClassImage || ptr_type.storage == StorageClassImage) && opcode == OpAtomicOr)\n"
       "\t\t{\n"
       "\t\t\t// The image operand is an OpImageTexelPointer virtual expression\n"
       "\t\t\t// (\"img@coord\"); split it into the image and the coordinate. The\n"
       "\t\t\t// value operand is op1 (op2 is unused in this RMW form).\n"
       "\t\t\tauto obj_expression = to_expression(obj);\n"
       "\t\t\tauto split_index = obj_expression.find_first_of('@');\n"
       "\t\t\tstring image_expr = obj_expression;\n"
       "\t\t\tstring coord_expr;\n"
       "\t\t\tif (split_index != string::npos)\n"
       "\t\t\t{\n"
       "\t\t\t\timage_expr = obj_expression.substr(0, split_index);\n"
       "\t\t\t\tcoord_expr = obj_expression.substr(split_index + 1);\n"
       "\t\t\t}\n"
       "\t\t\telse\n"
       "\t\t\t{\n"
       "\t\t\t\t// Direct image form (no texel pointer): the coordinate is op1.\n"
       "\t\t\t\tcoord_expr = to_expression(op1);\n"
       "\t\t\t}\n"
       "\t\t\tstring val_expr = split_index != string::npos ? to_expression(op1) : to_expression(op2);\n"
       "\t\t\tstring low = join(\"uint(\", val_expr, \" & 0xFFFFFFFFu)\");\n"
       "\t\t\tstring high = join(\"uint(\", val_expr, \" >> 32u)\");\n"
       "\t\t\tstring call = join(image_expr, \".atomic_fetch_or(\", coord_expr, \", uint4(\", low, \", \", high, \", 0u, 0u))\");\n"
       "\t\t\t// Mirror the upstream check_discard (declared after this block).\n"
       "\t\t\tbool mvk_check_discard = opcode != OpAtomicLoad && needs_frag_discard_checks() &&\n"
       "\t\t\t                         ptr_type.storage != StorageClassWorkgroup;\n"
       "\t\t\tif (mvk_check_discard)\n"
       "\t\t\t{\n"
       "\t\t\t\temit_uninitialized_temporary_expression(result_type, result_id);\n"
       "\t\t\t\tstatement(\"if (!\", builtin_to_glsl(BuiltInHelperInvocation, StorageClassInput), \")\");\n"
       "\t\t\t\tbegin_scope();\n"
       "\t\t\t\tstatement(\"uint4 mvkFbAtomic = \", call, \";\");\n"
       "\t\t\t\tstatement(to_expression(result_id), \" = (((uint64_t)(mvkFbAtomic.y)) << 32u) | (uint64_t)(mvkFbAtomic.x);\");\n"
       "\t\t\t\tend_scope();\n"
       "\t\t\t\tstatement(\"else\");\n"
       "\t\t\t\tbegin_scope();\n"
       "\t\t\t\tstatement(to_expression(result_id), \" = {};\");\n"
       "\t\t\t\tend_scope();\n"
       "\t\t\t}\n"
       "\t\t\telse\n"
       "\t\t\t{\n"
       "\t\t\t\tstatement(\"uint4 mvkFbAtomic = \", call, \";\");\n"
       "\t\t\t\texp = join(\"(((uint64_t)(mvkFbAtomic.y)) << 32u) | (uint64_t)(mvkFbAtomic.x)\");\n"
       "\t\t\t\tif (expected_type != type.basetype)\n"
       "\t\t\t\t\texp = bitcast_expression(type, expected_type, exp);\n"
       "\t\t\t\temit_op(result_type, result_id, exp, false);\n"
       "\t\t\t}\n"
       "\t\t\tflush_all_atomic_capable_variables();\n"
       "\t\t\treturn;\n"
       "\t\t}\n"
       "\t\telse\n"
       "\t\t\tSPIRV_CROSS_THROW(\"MSL currently does not support 64-bit atomics.\");\n"
       "\t}")
assert s.count(old) == 1, "atomic"
s = s.replace(old, new)
# 3. the 64-bit simd-OR -> two 32-bit simd-ORs (statement form)
old = "\tMSL_GROUP_OP(BitwiseAnd, and)\n\tMSL_GROUP_OP(BitwiseOr, or)\n\tMSL_GROUP_OP(BitwiseXor, xor)"
new = ("\tMSL_GROUP_OP(BitwiseAnd, and)\n"
       "\tcase OpGroupNonUniformBitwiseOr:\n"
       "\t{\n"
       "\t\t// MVK: the 64-bit simd-OR (the sampler-feedback group ballot) is\n"
       "\t\t// emulated as two 32-bit simd-ORs.\n"
       "\t\tauto operation = static_cast<GroupOperation>(ops[op_idx++]);\n"
       "\t\tif (operation == GroupOperationReduce && get<SPIRType>(result_type).width == 64)\n"
       "\t\t{\n"
       "\t\t\tstring val = to_unpacked_expression(ops[op_idx]);\n"
       "\t\t\tstatement(\"uint64_t mvkFbSimdOr = (uint64_t)simd_or((uint)(\", val, \" & 0xFFFFFFFFu)) | (((uint64_t)simd_or((uint)(\", val, \" >> 32u))) << 32u);\");\n"
       "\t\t\tset<SPIRExpression>(id, \"mvkFbSimdOr\", result_type, true);\n"
       "\t\t}\n"
       "\t\telse if (operation == GroupOperationReduce)\n"
       "\t\t\temit_unary_func_op(result_type, id, ops[op_idx], \"simd_or\");\n"
       "\t\telse if (operation == GroupOperationInclusiveScan)\n"
       "\t\t\tSPIRV_CROSS_THROW(\"Metal doesn't support InclusiveScan for OpGroupNonUniformBitwiseOr.\");\n"
       "\t\telse if (operation == GroupOperationExclusiveScan)\n"
       "\t\t\tSPIRV_CROSS_THROW(\"Metal doesn't support ExclusiveScan for OpGroupNonUniformBitwiseOr.\");\n"
       "\t\telse if (operation == GroupOperationClusteredReduce)\n"
       "\t\t{\n"
       "\t\t\tuint32_t cluster_size = evaluate_constant_u32(ops[op_idx + 1]);\n"
       "\t\t\tif (get_execution_model() != ExecutionModelFragment || msl_options.supports_msl_version(2, 2))\n"
       "\t\t\t\tadd_spv_func_and_recompile(SPVFuncImplSubgroupClusteredBitwiseOr);\n"
       "\t\t\temit_subgroup_cluster_op(result_type, id, cluster_size, ops[op_idx], \"or\");\n"
       "\t\t}\n"
       "\t\telse\n"
       "\t\t\tSPIRV_CROSS_THROW(\"Invalid group operation.\");\n"
       "\t\tbreak;\n"
       "\t}\n"
       "\tMSL_GROUP_OP(BitwiseXor, xor)")
assert s.count(old) == 1, "simd"
s = s.replace(old, new)
open(p, "w").write(s)
print("int64-image-atomic patch applied")
PATCH2
# The atomic-aware storage-image access fixup: the upstream
# fixup_image_load_store_access() marks every storage image non-writable and
# non-readable, and the image-atomic path only loosens the direct backing
# variable of an OpImageTexelPointer. For sampler-feedback shaders the
# atomic image is reached through an array element + a function parameter,
# so the descriptor variable stays restricted and the emitted MSL type
# loses the read_write access qualifier. Replace the MSL fixup with an
# atomic-aware version that keeps images used by image atomics read-write.
python3 - "$SPIRV_MSL" <<'PATCH3'
import sys
p = sys.argv[1]
s = open(p).read()

# 1. replace the fixup call in CompilerMSL::compile()
old = "\tfixup_image_load_store_access();\n"
if s.count(old) != 1:
    sys.exit(0)  # not spirv_msl.cpp (e.g. spirv_cross.cpp)
s = s.replace(old, "\tfixup_image_load_store_access_atomic_aware();\n")

# 2. insert the atomic-aware fixup before image_type_glsl
anchor = "string CompilerMSL::image_type_glsl(const SPIRType &type, uint32_t id, bool member)"
if s.count(anchor) != 1:
    sys.exit(0)  # not spirv_msl.cpp
new_method = r'''// MVK fork: the upstream fixup marks every storage image non-writable and
// non-readable, and the image-atomic path only loosens the direct backing
// variable of the OpImageTexelPointer. For sampler-feedback shaders the
// atomic image is reached through an array element + a function parameter,
// so the descriptor variable stays restricted and the emitted MSL type
// loses the read_write access qualifier. Skip storage images that are used
// by any image atomic (OpImageTexelPointer reaching a variable through
// access chains and function-call arguments).
void CompilerMSL::fixup_image_load_store_access_atomic_aware()
{
	std::unordered_set<uint32_t> atomic_bases;
	{
		std::unordered_map<uint32_t, uint32_t> chain_bases;
		std::unordered_map<uint32_t, uint32_t> param_owner;
		std::unordered_map<uint32_t, uint32_t> param_index;
		std::unordered_map<uint32_t, uint32_t> param_count;
		std::vector<std::pair<uint32_t, std::vector<uint32_t>>> calls;
		std::vector<uint32_t> texel_ptrs;
		uint32_t cur_func = 0;

		const auto &spirv = ir.spirv;
		size_t i = 5; // skip the SPIR-V module header
		while (i < spirv.size())
		{
			uint32_t wc = spirv[i] >> 16;
			Op op = static_cast<Op>(spirv[i] & 0xFFFFu);
			if (wc == 0 || i + wc > spirv.size())
				break;
			const uint32_t *args = &spirv[i + 1];
			switch (op)
			{
			case OpFunction:
				cur_func = args[1];
				break;
			case OpFunctionParameter:
				param_owner[args[1]] = cur_func;
				param_index[args[1]] = param_count[cur_func]++;
				break;
			case OpAccessChain:
			case OpInBoundsAccessChain:
				chain_bases[args[1]] = args[2];
				break;
			case OpFunctionCall:
			{
				std::vector<uint32_t> arg_ids;
				for (uint32_t k = 3; k < wc; k++)
					arg_ids.push_back(args[k]);
				calls.push_back({ args[2], std::move(arg_ids) });
				break;
			}
			case OpImageTexelPointer:
				texel_ptrs.push_back(args[2]);
				break;
			default:
				break;
			}
			i += wc;
		}

		std::function<void(uint32_t, std::unordered_set<uint32_t> &, std::unordered_set<uint32_t> &)> resolve =
		    [&](uint32_t id, std::unordered_set<uint32_t> &out, std::unordered_set<uint32_t> &seen) -> void
		{
			if (seen.count(id))
				return;
			seen.insert(id);
			auto it = chain_bases.find(id);
			if (it != chain_bases.end())
			{
				resolve(it->second, out, seen);
				return;
			}
			auto pit = param_owner.find(id);
			if (pit != param_owner.end())
			{
				uint32_t owner = pit->second;
				uint32_t idx = param_index[id];
				for (auto &c : calls)
				{
					if (c.first == owner && idx < c.second.size())
						resolve(c.second[idx], out, seen);
				}
				return;
			}
			out.insert(id);
		};

		for (auto tp : texel_ptrs)
		{
			std::unordered_set<uint32_t> seen;
			resolve(tp, atomic_bases, seen);
		}
	}

	ir.for_each_typed_id<SPIRVariable>([&](uint32_t var, const SPIRVariable &) {
		auto &vartype = expression_type(var);
		if (vartype.basetype == SPIRType::Image && vartype.image.sampled == 2)
		{
			// Images used by image atomics must stay read-write.
			if (atomic_bases.count(var))
				return;
			if (!has_decoration(var, DecorationNonWritable) && !has_decoration(var, DecorationNonReadable))
			{
				set_decoration(var, DecorationNonWritable);
				set_decoration(var, DecorationNonReadable);
			}
		}
	});
}

'''
s = s.replace(anchor, new_method + anchor)
open(p, "w").write(s)
print("fixup applied")
PATCH3

# The hpp declaration (same directory as the cpp).
python3 - "${SPIRV_MSL%.cpp}.hpp" <<'PATCH4'
import sys
p = sys.argv[1]
s = open(p).read()
old = "\tstd::string image_type_glsl(const SPIRType &type, uint32_t id, bool member) override;"
if s.count(old) != 1:
    sys.exit(0)  # not spirv_msl.hpp
new = "\tstd::string image_type_glsl(const SPIRType &type, uint32_t id, bool member) override;\n\tvoid fixup_image_load_store_access_atomic_aware();"
s = s.replace(old, new)
open(p, "w").write(s)
print("fixup hpp declaration applied")
PATCH4
