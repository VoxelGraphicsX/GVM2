#!/usr/bin/env node

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const toolsDirectory = path.dirname( fileURLToPath( import.meta.url ) );
const repositoryRoot = path.resolve( toolsDirectory, '..', '..' );
const manifestDirectory = path.resolve( toolsDirectory, '..', 'Manifest' );
const defaultAuditPath = path.join( manifestDirectory, 'three-r185-capability-audit.json' );
const defaultFreezePath = path.join( manifestDirectory, 'phase1-capability-freeze.json' );
const defaultOutputPath = path.join( manifestDirectory, 'three-r185-capability-adjudication.json' );
const expectedCommit = '2431a09f46f34c560bc8e44b33be0e567723d5b9';

const supplementalUpstreamEvidence = new Map( [
	[ 'webgl_animation_skinning_ik::dsl_texture_cube', [
		{ sourcePath: 'examples/webgl_animation_skinning_ik.html', line: 178, column: 5, detector: 'manual_semantic_review', role: 'example_source', excerpt: 'mirrorSphereCamera.update( renderer, scene );' }
	] ],
	[ 'webgl_lightprobe::dsl_texture_cube', [
		{ sourcePath: 'examples/webgl_lightprobe.html', line: 108, column: 6, detector: 'manual_semantic_review', role: 'example_source', excerpt: 'lightProbe.copy( LightProbeGenerator.fromCubeTexture( cubeTexture ) );' }
	] ],
	[ 'webgpu_lightprobe::dsl_texture_cube', [
		{ sourcePath: 'examples/webgpu_lightprobe.html', line: 119, column: 6, detector: 'manual_semantic_review', role: 'example_source', excerpt: 'lightProbe.copy( LightProbeGenerator.fromCubeTexture( cubeTexture ) );' }
	] ],
	[ 'webgl_lightprobe_cubecamera::dsl_texture_cube', [
		{ sourcePath: 'examples/webgl_lightprobe_cubecamera.html', line: 89, column: 6, detector: 'manual_semantic_review', role: 'example_source', excerpt: 'cubeCamera.update( renderer, scene );' },
		{ sourcePath: 'examples/webgl_lightprobe_cubecamera.html', line: 91, column: 20, detector: 'manual_semantic_review', role: 'example_source', excerpt: 'LightProbeGenerator.fromCubeRenderTarget( renderer, cubeRenderTarget )' }
	] ],
	[ 'webgpu_lightprobe_cubecamera::dsl_texture_cube', [
		{ sourcePath: 'examples/webgpu_lightprobe_cubecamera.html', line: 105, column: 6, detector: 'manual_semantic_review', role: 'example_source', excerpt: 'cubeCamera.update( renderer, scene );' },
		{ sourcePath: 'examples/webgpu_lightprobe_cubecamera.html', line: 107, column: 20, detector: 'manual_semantic_review', role: 'example_source', excerpt: 'LightProbeGenerator.fromCubeRenderTarget( renderer, cubeRenderTarget )' }
	] ],
	[ 'webgpu_cubemap_dynamic::dsl_texture_cube', [
		{ sourcePath: 'examples/webgpu_cubemap_dynamic.html', line: 174, column: 5, detector: 'manual_semantic_review', role: 'example_source', excerpt: 'cubeCamera.update( renderer, scene );' }
	] ],
	[ 'webgpu_materials_envmaps_bpcem::dsl_texture_cube', [
		{ sourcePath: 'examples/webgpu_materials_envmaps_bpcem.html', line: 229, column: 5, detector: 'manual_semantic_review', role: 'example_source', excerpt: 'cubeCamera.update( renderer, scene );' }
	] ],
	[ 'webgpu_materials_envmaps_groundprojected::dsl_texture_cube', [
		{ sourcePath: 'examples/webgpu_materials_envmaps_groundprojected.html', line: 146, column: 5, detector: 'manual_semantic_review', role: 'example_source', excerpt: 'cubeRenderTarget.fromEquirectangularTexture( renderer, envMap );' },
		{ sourcePath: 'examples/webgpu_materials_envmaps_groundprojected.html', line: 153, column: 25, detector: 'manual_semantic_review', role: 'example_source', excerpt: 'cubeTexture( cubeMap, getGroundProjectedNormal' }
	] ],
	[ 'webgpu_pmrem_scene::dsl_texture_cube', [
		{ sourcePath: 'examples/webgpu_pmrem_scene.html', line: 111, column: 21, detector: 'manual_semantic_review', role: 'example_source', excerpt: 'new THREE.PMREMGenerator( renderer ).fromScene( scene )' }
	] ],
	[ 'webgl_materials_texture_manualmipmap::automatic_mipmap_generation', [
		{ sourcePath: 'examples/webgl_materials_texture_manualmipmap.html', line: 114, column: 5, detector: 'manual_semantic_review', role: 'example_source', excerpt: 'textureCanvas1.mipmaps[ 0 ] = canvas;' }
	] ],
	[ 'webgpu_materials_texture_manualmipmap::automatic_mipmap_generation', [
		{ sourcePath: 'examples/webgpu_materials_texture_manualmipmap.html', line: 107, column: 5, detector: 'manual_semantic_review', role: 'example_source', excerpt: 'textureCanvas1.mipmaps[ 0 ] = canvas;' }
	] ],
	[ 'webgl_materials_cubemap_render_to_mipmaps::automatic_mipmap_generation', [
		{ sourcePath: 'examples/webgl_materials_cubemap_render_to_mipmaps.html', line: 122, column: 5, detector: 'manual_semantic_review', role: 'example_source', excerpt: 'rt.texture.mipmaps.push( {} );' },
		{ sourcePath: 'examples/webgl_materials_cubemap_render_to_mipmaps.html', line: 156, column: 6, detector: 'manual_semantic_review', role: 'example_source', excerpt: 'cubeCamera.update( renderer, mesh );' }
	] ],
	[ 'webgl_materials_wireframe::sample_mask_builtin', [
		{ sourcePath: 'examples/webgl_materials_wireframe.html', line: 81, column: 16, detector: 'manual_semantic_review', role: 'example_source', excerpt: 'renderer = new THREE.WebGLRenderer( { antialias: true } );' },
		{ sourcePath: 'examples/webgl_materials_wireframe.html', line: 108, column: 7, detector: 'manual_semantic_review', role: 'example_source', excerpt: 'wireframe: true' }
	] ],
	[ 'webgl_mesh_batch::render_set_sort', [
		{ sourcePath: 'examples/webgl_mesh_batch.html', line: 71, column: 4, detector: 'manual_semantic_review', role: 'example_source', excerpt: 'transparent: true,' },
		{ sourcePath: 'examples/webgl_mesh_batch.html', line: 444, column: 5, detector: 'manual_semantic_review', role: 'example_source', excerpt: 'mesh.setCustomSort( api.useCustomSort ? sortFunction : null );' }
	] ],
	[ 'webgpu_mesh_batch::render_set_sort', [
		{ sourcePath: 'examples/webgpu_mesh_batch.html', line: 74, column: 4, detector: 'manual_semantic_review', role: 'example_source', excerpt: 'useCustomSort: true,' },
		{ sourcePath: 'examples/webgpu_mesh_batch.html', line: 311, column: 6, detector: 'manual_semantic_review', role: 'example_source', excerpt: 'mesh.setCustomSort( api.useCustomSort ? sortFunction : null );' }
	] ],
	[ 'webgl_depth_texture::multisample_resolve', [
		{ sourcePath: 'examples/webgl_depth_texture.html', line: 92, column: 5, detector: 'manual_semantic_review', role: 'example_source', excerpt: 'samples: 0,' }
	] ],
	[ 'webgl_multiple_rendertargets::multisample_resolve', [
		{ sourcePath: 'examples/webgl_multiple_rendertargets.html', line: 136, column: 5, detector: 'manual_semantic_review', role: 'example_source', excerpt: 'samples: 4,' }
	] ],
	[ 'webgpu_multisampled_renderbuffers::multisample_resolve', [
		{ sourcePath: 'examples/webgpu_multisampled_renderbuffers.html', line: 55, column: 5, detector: 'manual_semantic_review', role: 'example_source', excerpt: 'multisampling: true' },
		{ sourcePath: 'examples/webgpu_multisampled_renderbuffers.html', line: 129, column: 6, detector: 'manual_semantic_review', role: 'example_source', excerpt: 'samples: params.multisampling ? 4 : 1,' }
	] ],
	[ 'webgpu_occlusion::occlusion_query', [
		{ sourcePath: 'examples/webgpu_occlusion.html', line: 65, column: 26, detector: 'manual_semantic_review', role: 'example_source', excerpt: 'frame.renderer.isOccluded( this.testObject )' },
		{ sourcePath: 'examples/webgpu_occlusion.html', line: 67, column: 6, detector: 'manual_semantic_review', role: 'example_source', excerpt: 'isOccluded ? this.occludedColor : this.normalColor' }
	] ]
] );

const textureArrayEvidence = [
	{
		path: 'GVM/UGLHeaders/Details/UGL.Resources.h',
		line: 931,
		excerpt: 'HLSLType sample(Sampler s, float2 uv, uint layerIndex) const'
	},
	{
		path: 'GVM/UGLHeaders/Details/UGL.Resources.h',
		line: 935,
		excerpt: 'HLSLType sampleLevel(Sampler s, float2 uv, uint layerIndex, float lod) const'
	},
	{
		path: 'UGLC/Source/CodeGen/Experimental/UGLIR/UGLIRCore.hpp',
		line: 296,
		excerpt: 'Texture2DArray,'
	},
	{
		path: 'GVM/GVMRHI/GVMRHI.hpp',
		line: 546,
		excerpt: 'e2DArray = 0x00000003,'
	},
	{
		path: 'tests/runners/uglc/node/test-registry.mjs',
		line: 436,
		excerpt: "'texture_dimension \"2d_array\"'"
	}
];

const mipEvidence = [
	{
		path: 'GVM/UGLHeaders/Details/UGL.Device.h',
		line: 22,
		excerpt: 'uint32_t mipLevelCount = 1, uint32_t layerCount = 1'
	},
	{
		path: 'GVM/UGLHeaders/Details/UGL.Resources.h',
		line: 416,
		excerpt: 'uint baseMipLevel = 0;'
	},
	{
		path: 'GVM/UGLHeaders/Details/UGL.Resources.h',
		line: 633,
		excerpt: 'HLSLType sampleLevel(Sampler s, float2 uv, float lod) const'
	},
	{
		path: 'GVM/UGLHeaders/Details/UGL.Queue.h',
		line: 124,
		excerpt: 'uint32_t mipLevelOffset = 0, uint32_t arrayLayerOffset = 0'
	},
	{
		path: 'tests/integration/rhi/test_rhi_spd_hiz_readback.cpp',
		line: 279,
		excerpt: '.baseMipLevel = mip,'
	}
];

const renderSetOrderingEvidence = [
	{
		path: 'GVM/UGLHeaders/Details/UGL.RenderSet.h',
		line: 167,
		excerpt: 'RenderEntity<T> alloc(const RenderSetAllocInfo &info)'
	},
	{
		path: 'GVM/UGLHeaders/Details/UGL.RenderSet.h',
		line: 180,
		excerpt: 'void remove(RenderEntity<T> entity)'
	},
	{
		path: 'GVM/UGLHeaders/Details/UGL.RenderSet.h',
		line: 230,
		excerpt: 'void update()'
	},
	{
		path: 'GVM/GVMCore/Private/GShader.cpp',
		line: 76,
		excerpt: 'passEncoder->drawIndexedIndirect('
	}
];

const billboardEvidence = [
	{
		path: 'GVM/UGLHeaders/Details/UGL.Attributes.h',
		line: 131,
		excerpt: '#define RenderEntityInstanceID UGL_ATTR(RenderEntityInstanceID)'
	},
	{
		path: 'GVM/UGLHeaders/Details/UGL.Pipeline.h',
		line: 59,
		excerpt: 'TriangleList = 0x00000003,'
	},
	{
		path: 'GVM/UGLHeaders/Details/UGL.RenderSet.h',
		line: 157,
		excerpt: 'uint32_t instanceCount = 1;'
	},
	{
		path: 'UGLC/Source/CodeGen/Experimental/UGLIR/UGLIRLowering.cpp',
		line: 3009,
		excerpt: 'BuiltinSemanticKind::DrawEntityInstanceID'
	},
	{
		path: 'tests/runners/uglc/node/test-registry.mjs',
		line: 3406,
		excerpt: 'MSL RenderEntityInstanceID should come from decoded command params.'
	}
];

const cullEvidence = [
	{
		path: 'GVM/UGLHeaders/Details/UGL.Shaders.h',
		line: 66,
		excerpt: 'void setCullMode(CullMode cullMode)'
	},
	{
		path: 'GVM/UGLHeaders/Details/UGL.Pipeline.h',
		line: 49,
		excerpt: 'Front = 0x00000001,'
	},
	{
		path: 'GVM/UGLHeaders/Details/UGL.Pipeline.h',
		line: 50,
		excerpt: 'Back = 0x00000002,'
	},
	{
		path: 'tests/runners/uglc/node/test-registry.mjs',
		line: 1856,
		excerpt: "setCullMode(GVM::RHI::CullMode::Back);"
	}
];

const fragmentAtomicEvidence = [
	{
		path: 'GVM/UGLHeaders/Details/UGL.Atomic.h',
		line: 40,
		excerpt: 'T atomicOr(T &atom, U value)'
	},
	{
		path: 'tests/uglc/fixtures/shader-static-variant-diagnostics-render/ShaderStaticVariantDiagnosticsRender.hpp',
		line: 84,
		excerpt: 'atomicAdd(diagnosticBindGroup->counters[0], 1u);'
	}
];

const singleSampleEvidence = [
	{
		path: 'GVM/UGLHeaders/Details/UGL.Device.h',
		line: 22,
		excerpt: 'Private::TextureProxy createTexture(string label, uint32_t width, uint32_t height'
	},
	{
		path: 'GVM/GVMRHI/GVMRHI.hpp',
		line: 1010,
		excerpt: 'struct RenderPassColorAttachment'
	}
];

const frozenStencilEvidence = [
	{
		path: 'GVMRuntime_ThreeSamples/Manifest/phase1-capability-freeze.json',
		line: 7,
		excerpt: '"complete_stencil_operations_and_reference"'
	},
	{
		path: 'GVM/GVMRHI/GVMRHI.hpp',
		line: 1174,
		excerpt: 'struct DepthStencilState'
	}
];

const frozenMultisampleEvidence = [
	{
		path: 'GVMRuntime_ThreeSamples/Manifest/phase1-capability-freeze.json',
		line: 6,
		excerpt: '"multisample_resolve"'
	},
	{
		path: 'GVM/GVMRHI/GVMRHI.hpp',
		line: 1010,
		excerpt: 'struct RenderPassColorAttachment'
	},
	{
		path: 'GVM/GVMRHI/GVMRHI.hpp',
		line: 1330,
		excerpt: 'struct RenderPipelineDescriptor'
	}
];

const frozenSampleMaskEvidence = [
	{
		path: 'GVMRuntime_ThreeSamples/Manifest/phase1-capability-freeze.json',
		line: 13,
		excerpt: '"sample_mask_builtin"'
	},
	{
		path: 'GVM/GVMRHI/GVMRHI.hpp',
		line: 1330,
		excerpt: 'struct RenderPipelineDescriptor'
	}
];

const frozenOcclusionEvidence = [
	{
		path: 'GVMRuntime_ThreeSamples/Manifest/phase1-capability-freeze.json',
		line: 15,
		excerpt: '"occlusion_query"'
	},
	{
		path: 'GVM/GVMRHI/GVMRHI.hpp',
		line: 855,
		excerpt: 'enum class QueryType'
	},
	{
		path: 'GVM/GVMRHI/GVMRHI.hpp',
		line: 857,
		excerpt: 'Timestamp = 0,'
	}
];

/** Reads a path-valued CLI option without using environment variables. */
function readPathOption( argumentsList, optionName, fallbackPath ) {

	const optionIndex = argumentsList.indexOf( optionName );
	if ( optionIndex === - 1 ) return fallbackPath;
	if ( optionIndex + 1 >= argumentsList.length ) throw new Error( `Missing value for ${ optionName }.` );
	return path.resolve( argumentsList[ optionIndex + 1 ] );

}

/** Returns a stable key for one example/capability adjudication pair. */
function decisionKey( exampleId, capability ) {

	return `${ exampleId }::${ capability }`;

}

/** Adds one manually reviewed decision and rejects accidental duplicate mappings. */
function addDecision( decisionMap, exampleId, capability, decision ) {

	const key = decisionKey( exampleId, capability );
	if ( decisionMap.has( key ) ) throw new Error( `Duplicate adjudication mapping for ${ key }.` );
	decisionMap.set( key, decision );

}

/** Adds the same reviewed decision template for a bounded list of semantically equivalent cases. */
function addDecisions( decisionMap, exampleIds, capability, decision ) {

	for ( const exampleId of exampleIds ) addDecision( decisionMap, exampleId, capability, decision );

}

/** Constructs the complete hand-reviewed mapping without reading or mutating the main manifest. */
function buildManualDecisionMap() {

	const decisions = new Map();

	addDecisions( decisions, [
		'webgl_effects_anaglyph',
		'webgl_effects_parallaxbarrier',
		'webgl_effects_stereo',
		'webgl_geometry_teapot',
		'webgl_materials_cubemap',
		'webgl_materials_cubemap_refraction',
		'webgl_materials_envmaps',
		'webgl_postprocessing_backgrounds',
		'webgl_postprocessing_dof',
		'webgl_postprocessing_dof2',
		'webgpu_display_stereo',
		'webgpu_instance_uniform',
		'webgpu_materials_basic',
		'webgpu_materials_displacementmap',
		'webgpu_materials_envmaps',
		'webaudio_orientation'
	], 'dsl_texture_cube', {
		decision: 'expressible_existing_dsl',
		coreBehaviorSummary: 'The upstream case consumes six cube faces for a background, reflection, refraction, or material sample; native cube resource identity is not visible in the accepted image.',
		existingDslEquivalentPath: 'Decode the six faces in C++, upload them as six layers of the existing Texture2DArray (or a guttered 2D atlas), and use a private DSL direction-to-face/UV function. Reflection and refraction only change the direction calculation before the same array/atlas sample.',
		repositoryCapabilityEvidence: textureArrayEvidence,
		reentryTest: 'Render the canonical reflection/refraction/background state through all four quadrants and compare both the Three oracle and cross-quadrant images at the global thresholds.'
	} );

	addDecisions( decisions, [
		'webgl_animation_skinning_ik',
		'webgpu_cubemap_dynamic',
		'webgpu_materials_envmaps_bpcem'
	], 'dsl_texture_cube', {
		decision: 'expressible_existing_dsl',
		coreBehaviorSummary: 'The upstream case captures a live environment with CubeCamera and samples that capture as an environment map.',
		existingDslEquivalentPath: 'Render the same Scene RenderSet through six DSL scene passes with the six cube view/projection matrices into six Texture2DArray layers or atlas tiles. Generate seam gutters in an existing compute pass, then sample through a private direction-to-face/UV DSL function.',
		repositoryCapabilityEvidence: [ ...textureArrayEvidence, ...mipEvidence ],
		reentryTest: 'Capture a fixed non-zero animation frame, assert six face passes reuse the Scene RenderSet, and compare the reflected result in Legacy/Experimental on Metal/Vulkan.'
	} );

	addDecisions( decisions, [
		'webgl_lightprobe',
		'webgpu_lightprobe'
	], 'dsl_texture_cube', {
		decision: 'expressible_existing_dsl',
		coreBehaviorSummary: 'The loaded six-face image is used both as a visible environment and as the source for low-order spherical-harmonic light-probe coefficients.',
		existingDslEquivalentPath: 'Upload the faces to Texture2DArray/atlas, sample the visible environment with direction-to-face mapping, and deterministically integrate the fixed face pixels into SH coefficients before the DSL lighting pass. No cube-typed shader resource is required.',
		repositoryCapabilityEvidence: textureArrayEvidence,
		reentryTest: 'Compare the canonical probe-lit model and probe helper against the Three oracle while recording the six-layer texture and SH coefficient snapshot.'
	} );

	addDecisions( decisions, [
		'webgl_lightprobe_cubecamera',
		'webgpu_lightprobe_cubecamera'
	], 'dsl_texture_cube', {
		decision: 'expressible_existing_dsl',
		coreBehaviorSummary: 'The case captures six scene views and derives a light probe from the captured texels.',
		existingDslEquivalentPath: 'Render six DSL passes into array layers/atlas tiles and run an existing DSL compute reduction over those texels to produce SH coefficients consumed by the DSL light implementation. The capture and all scene redraws reuse the one Scene RenderSet.',
		repositoryCapabilityEvidence: [ ...textureArrayEvidence, ...fragmentAtomicEvidence ],
		reentryTest: 'At the canonical capture frame, verify six capture passes, deterministic SH coefficients, and oracle-equivalent lighting in all quadrants.'
	} );

	addDecisions( decisions, [
		'webgl_materials_cubemap_mipmaps',
		'webgpu_materials_cubemap_mipmaps',
		'webgl_materials_cubemap_render_to_mipmaps'
	], 'dsl_texture_cube', {
		decision: 'expressible_existing_dsl',
		coreBehaviorSummary: 'The case samples six-face environment data across explicit mip levels; one case renders filtered values into every cube mip.',
		existingDslEquivalentPath: 'Represent faces as six array layers or atlas regions and retain every explicit mip. Existing sampleLevel/sampleGrad operations select LOD; DSL render/compute passes fill each mip and update cross-face gutters without a TextureCube declaration.',
		repositoryCapabilityEvidence: [ ...textureArrayEvidence, ...mipEvidence ],
		reentryTest: 'Render both custom and generated mip spheres at the canonical camera distance and compare all mip transitions and face seams in the four quadrants.'
	} );

	addDecision( decisions, 'webgpu_materials_envmaps_groundprojected', 'dsl_texture_cube', {
		decision: 'expressible_existing_dsl',
		coreBehaviorSummary: 'The case converts an equirectangular HDR image to six directions and samples it with a ground-projected direction.',
		existingDslEquivalentPath: 'Keep the source as an existing Texture2D and apply the ground-projected direction followed by equirectangular UV mapping directly in private DSL, or preconvert it into Texture2DArray layers. The final sampling function needs no cube resource type.',
		repositoryCapabilityEvidence: [ ...textureArrayEvidence, ...mipEvidence ],
		reentryTest: 'Capture the non-default ground-projection radius/height state and compare the horizon and ground contact region against the oracle.'
	} );

	addDecision( decisions, 'webgpu_pmrem_scene', 'dsl_texture_cube', {
		decision: 'expressible_existing_dsl',
		coreBehaviorSummary: 'The case convolves a Scene into roughness-dependent environment levels and displays a six-face background.',
		existingDslEquivalentPath: 'Use six array layers/atlas tiles for the environment and existing DSL render/compute passes to perform the PMREM convolution into explicit mip or atlas regions. Private direction-to-face sampling supplies the material lookup.',
		repositoryCapabilityEvidence: [ ...textureArrayEvidence, ...mipEvidence ],
		reentryTest: 'Compare the canonical roughness series and background seams while validating the explicit PMREM level count in each quadrant.'
	} );

	addDecisions( decisions, [
		'webgl_depth_texture',
		'webgl_lights_spotlight',
		'webgl_materials_texture_filters',
		'webgl_materials_texture_partialupdate',
		'webgl_shadow_contact',
		'webgl_postprocessing_masking',
		'webgl_postprocessing_pixel',
		'webgl_postprocessing_taa',
		'webgpu_compute_particles_rain',
		'webgpu_compute_particles_snow',
		'webgpu_compute_texture_3d',
		'webgpu_lights_projector',
		'webgpu_lights_spotlight',
		'webgpu_postprocessing_masking',
		'webgpu_postprocessing_pixel',
		'webgpu_shadow_contact',
		'webgpu_textures_partialupdate'
	], 'automatic_mipmap_generation', {
		decision: 'not_core_behavior',
		coreBehaviorSummary: 'The upstream line explicitly disables mip generation; the accepted path uses a single level and therefore does not request the frozen automatic-generation capability.',
		whyNotCoreBehavior: 'The detector records any generateMipmaps assignment, including false. Here false is a negative configuration signal rather than a dependency on automatic mip generation.',
		existingDslEquivalentPath: 'Create a one-mip texture and use the requested nearest/linear sampler. Partial updates, render-target production, masking, lighting, or particle logic proceeds with existing DSL resources and passes.',
		repositoryCapabilityEvidence: [ ...singleSampleEvidence, ...mipEvidence.slice( 1, 3 ) ],
		reentryTest: 'Assert mipLevelCount=1 in the resource snapshot and compare the canonical image without any generated mip pass.'
	} );

	addDecisions( decisions, [
		'webgl_materials_texture_manualmipmap',
		'webgpu_materials_texture_manualmipmap'
	], 'automatic_mipmap_generation', {
		decision: 'not_core_behavior',
		coreBehaviorSummary: 'The case supplies authored mip images and compares sampler filters; it does not ask the renderer to synthesize those levels.',
		whyNotCoreBehavior: 'A mipmap minification filter is not evidence that automatic generation is required when the source explicitly provides the mip chain.',
		existingDslEquivalentPath: 'Create the texture with the authored mip count, upload each level through the existing mipLevelOffset parameter, and select nearest/linear mip filtering with SamplerDescriptor.',
		repositoryCapabilityEvidence: mipEvidence,
		reentryTest: 'Validate every uploaded authored mip color and compare the two filter variants at the fixed camera state.'
	} );

	addDecision( decisions, 'webgl_materials_cubemap_render_to_mipmaps', 'automatic_mipmap_generation', {
		decision: 'not_core_behavior',
		coreBehaviorSummary: 'The upstream case intentionally disables automatic generation and explicitly renders every target mip level.',
		whyNotCoreBehavior: 'Its core behavior is manual render-to-mip, so the frozen automatic generator is neither called nor semantically required.',
		existingDslEquivalentPath: 'Allocate explicit mip levels and bind a baseMipLevel view for each DSL filtering pass. The existing sampleLevel operation reads the source LOD.',
		repositoryCapabilityEvidence: mipEvidence,
		reentryTest: 'Verify each target mip is written by a DSL pass and compare the final mipmapped environment sphere against Three.'
	} );

	addDecisions( decisions, [
		'webgl_materials_cubemap_mipmaps',
		'webgpu_cubemap_adjustments',
		'webgpu_materials_cubemap_mipmaps'
	], 'automatic_mipmap_generation', {
		decision: 'expressible_existing_dsl',
		coreBehaviorSummary: 'The case needs a deterministic mip chain for static loaded images, including an authored-versus-generated comparison.',
		existingDslEquivalentPath: 'Allocate all mip levels with the current texture API and deterministically precompute or generate each downsample level with existing DSL compute/render passes. Upload/select levels with mip views and sampleLevel/mipmap filtering.',
		repositoryCapabilityEvidence: mipEvidence,
		reentryTest: 'Compare every visible LOD transition in the canonical static view and validate the stored mip count and per-level checksums.'
	} );

	addDecisions( decisions, [
		'webgpu_cubemap_dynamic',
		'webgpu_materials_envmaps_bpcem',
		'webgpu_reflection_roughness',
		'webgpu_texturegather'
	], 'automatic_mipmap_generation', {
		decision: 'expressible_existing_dsl',
		coreBehaviorSummary: 'The case consumes mip-filtered data produced by a render target, so the levels must be refreshed after the deterministic source pass.',
		existingDslEquivalentPath: 'Create explicit mip views and schedule existing DSL downsample render/compute passes after the source render. Use sampleLevel or the existing mipmap sampler for later reflection, PMREM, roughness, or gather work.',
		repositoryCapabilityEvidence: mipEvidence,
		reentryTest: 'At a fixed non-zero frame, verify every mip is refreshed after its source pass and compare the rough/reflected/gathered result in all quadrants.'
	} );

	addDecisions( decisions, [
		'webgl_clipping_stencil',
		'webgl_lines_fat_raycasting',
		'webgl_loader_svg',
		'webgl_materials_car',
		'webgl_random_uv',
		'webgl_shadow_contact',
		'webgpu_backdrop_area',
		'webgpu_lines_fat_raycasting',
		'webgpu_loader_materialx',
		'webgpu_particles',
		'webgpu_shadow_contact',
		'webgpu_tsl_graph',
		'webgpu_tsl_wood'
	], 'render_set_sort', {
		decision: 'expressible_existing_dsl',
		coreBehaviorSummary: 'The upstream renderOrder values establish a deterministic, finite draw order; they do not require a general GPU-side RenderSet sort operation.',
		existingDslEquivalentPath: 'Allocate the Scene RenderSet entities in the declared order. Separate fixed pipeline states into RenderClass passes that bind the same RenderSet and reject other material phases, preserving the static order without adding sort/filter APIs.',
		repositoryCapabilityEvidence: renderSetOrderingEvidence,
		reentryTest: 'Record entity IDs and pass phases in the scene snapshot, then compare the overlap region against the Three oracle in all quadrants.'
	} );

	addDecision( decisions, 'webgl_decals', 'render_set_sort', {
		decision: 'expressible_existing_dsl',
		coreBehaviorSummary: 'Each new decal receives the current decals.length, so order is append-only until all decals are removed.',
		existingDslEquivalentPath: 'Allocate one entity per decal in append order. removeDecals maps to existing RenderSet remove calls; the next deterministic replay allocates the new sequence from a cleared set.',
		repositoryCapabilityEvidence: renderSetOrderingEvidence,
		reentryTest: 'Replay a fixed decal shot sequence, assert monotonically ordered entities, clear them, replay again, and compare both captures.'
	} );

	addDecisions( decisions, [
		'webgl_mesh_batch',
		'webgpu_mesh_batch'
	], 'render_set_sort', {
		decision: 'expressible_existing_dsl',
		coreBehaviorSummary: 'BatchedMesh sorts opaque or transparent subobjects by camera depth and can apply a deterministic custom comparator while a bounded subset moves.',
		existingDslEquivalentPath: 'Represent each sortable batched subobject as an entity in the single Scene RenderSet. Compute the same camera-depth/custom order in C++, remove the prior entities, and allocate them in sorted order before RenderSet update; this is the phase-1 remove/reallocate path and does not add a reorder API.',
		repositoryCapabilityEvidence: renderSetOrderingEvidence,
		reentryTest: 'Use seeded geometry/transforms, capture default and transparent non-default states at a fixed frame, assert entity order equals the upstream comparator, and compare all quadrants.'
	} );

	addDecisions( decisions, [
		'webgl_interactive_points',
		'webgl_points_waves',
		'webgl_buffergeometry_custom_attributes_particles',
		'webgl_custom_attributes_points',
		'webgl_custom_attributes_points2',
		'webgl_custom_attributes_points3',
		'webgl_gpgpu_protoplanet'
	], 'point_size_builtin', {
		decision: 'expressible_existing_dsl',
		coreBehaviorSummary: 'The vertex shader derives a per-point pixel diameter and the fragment shader uses point-local coordinates for a texture, circle, alpha test, or fog.',
		existingDslEquivalentPath: 'Use one RenderSet entity with one quad instance per logical point. The vertex shader reads per-instance center/size through RenderEntityInstanceID and expands six triangle-list vertices in clip/screen space; quad UV supplies the exact point-local coordinate to the fragment shader.',
		repositoryCapabilityEvidence: billboardEvidence,
		reentryTest: 'At initial and fixed non-zero frames, compare point diameters, circular/texture edges, alpha tests, and depth scaling against Three in all quadrants.'
	} );

	addDecision( decisions, 'webgl_materials_wireframe', 'front_facing_builtin', {
		decision: 'expressible_existing_dsl',
		coreBehaviorSummary: 'The only facing-dependent result is choosing one of two wireframe colors for the same double-sided geometry.',
		existingDslEquivalentPath: 'Draw the same Scene RenderSet in two DSL RenderClass passes: cull back faces and output the front color, then cull front faces and output the back color. Both passes use the same barycentric edge calculation and RenderSet.',
		repositoryCapabilityEvidence: cullEvidence,
		reentryTest: 'Rotate the mesh to expose both windings and compare front/back colors and wire widths at the canonical frame.'
	} );

	addDecision( decisions, 'webgl_materials_wireframe', 'sample_mask_builtin', {
		decision: 'deferred',
		reasonCode: 'deferred_missing_rhi_capability',
		coreBehaviorSummary: 'The example compares native wireframe rasterization against a barycentric shader whose smooth edge coverage explicitly depends on alphaToCoverage with an antialiased framebuffer.',
		missingCapability: 'Native polygon wireframe rasterization plus multisample render targets and alpha-to-coverage/sample-mask pipeline semantics for the side-by-side comparison.',
		whyCurrentDslCannotExpressIt: 'The frozen DSL/RHI has no polygon wireframe mode, sample count, sample mask, or alpha-to-coverage pipeline state. Triangle edge expansion with alpha blending or supersampling does not preserve native wireframe rasterization, per-sample depth/coverage, and edge-join behavior that this comparison demonstrates.',
		minimumFutureApi: 'Expose polygon wireframe mode, multisample count, and alpha-to-coverage/sample-mask state in RenderClass and the RHI pipeline/pass contract, implemented consistently by Metal and Vulkan.',
		repositoryCapabilityEvidence: frozenSampleMaskEvidence,
		reentryTest: 'Render the default native wireframe and alpha-to-coverage barycentric wireframe side by side at multiple thicknesses and rotations, then pass the global Oracle and cross-quadrant thresholds.'
	} );

	addDecisions( decisions, [
		'webgl_lines_fat',
		'webgpu_lines_fat'
	], 'sample_mask_builtin', {
		decision: 'deferred',
		reasonCode: 'deferred_missing_rhi_capability',
		coreBehaviorSummary: 'The example directly compares wide-triangle line rendering with the platform line mode and exposes alpha-to-coverage as part of that line-rasterization comparison.',
		missingCapability: 'Multisample alpha-to-coverage/sample-mask behavior needed to preserve the native-line versus wide-line coverage comparison.',
		whyCurrentDslCannotExpressIt: 'The existing triangle-list expansion can reproduce wide-line geometry, but the frozen DSL/RHI cannot express multisample count, alpha-to-coverage, or sample masks, and cannot preserve the native line rasterization semantics being compared.',
		minimumFutureApi: 'Add multisample count and alpha-to-coverage/sample-mask pipeline state across DSL, RHI, Metal, and Vulkan, together with a canonical native-line comparison contract.',
		repositoryCapabilityEvidence: frozenSampleMaskEvidence,
		reentryTest: 'Capture the native and wide-line modes with alpha-to-coverage disabled and enabled, including joins and end caps, and pass all Oracle/cross-quadrant thresholds.'
	} );

	addDecisions( decisions, [
		'webgl_clipping',
		'webgl_clipping_intersection',
		'webgl_lines_fat_raycasting',
		'webgpu_clipping',
		'webgpu_compute_particles',
		'webgpu_instance_points',
		'webgpu_lines_fat_raycasting'
	], 'sample_mask_builtin', {
		decision: 'not_core_behavior',
		coreBehaviorSummary: 'Alpha-to-coverage only smooths resolved edges in this example; its named clipping, picking, compute, or instancing behavior does not expose sample-mask values as an observable result.',
		whyNotCoreBehavior: 'Canonical interaction coverage can exercise the example-specific behavior while an existing analytic coverage/private supersample DSL path reproduces resolved edges without claiming sample-mask API support.',
		existingDslEquivalentPath: 'Keep all scene and auxiliary GPU work in DSL, compute deterministic analytic edge coverage, and use the repository-wide fixed final downsample; do not expose or emulate a public sample-mask interface.',
		repositoryCapabilityEvidence: singleSampleEvidence,
		reentryTest: 'Exercise the named non-default behavior and compare resolved geometry edges plus the core result against the fixed Oracle in all four quadrants.'
	} );

	addDecision( decisions, 'webgpu_tsl_angular_slicing', 'front_facing_builtin', {
		decision: 'expressible_existing_dsl',
		coreBehaviorSummary: 'The sliced double-sided hull keeps the normal physical output on front faces and replaces back-face output with the slice color.',
		existingDslEquivalentPath: 'Use front-cull and back-cull RenderClass passes bound to the same Scene RenderSet. Apply the existing angular mask in both passes, emit physical material output in the front pass, and the uniform slice color in the back pass.',
		repositoryCapabilityEvidence: cullEvidence,
		reentryTest: 'Capture a non-default slice arc showing both surfaces and compare the cut boundary, front shading, and back color in all quadrants.'
	} );

	addDecision( decisions, 'webgl_depth_texture', 'multisample_resolve', {
		decision: 'not_core_behavior',
		coreBehaviorSummary: 'The depth-texture example defaults to samples=0; multisampling is an optional GUI branch rather than a requirement of the initial depth visualization.',
		whyNotCoreBehavior: 'The required initial frame uses the upstream default samples=0, and a non-default depth format/type state can satisfy interaction coverage without entering the unsupported samples branch.',
		existingDslEquivalentPath: 'Run the canonical scenarios with the existing single-sample color/depth attachments and sample the depth texture in the DSL post pass.',
		repositoryCapabilityEvidence: singleSampleEvidence,
		reentryTest: 'Assert samples remains 0 for initial and selected non-default format/type scenarios, then compare the depth visualization across all quadrants.'
	} );

	addDecision( decisions, 'webgl_multiple_rendertargets', 'multisample_resolve', {
		decision: 'deferred',
		reasonCode: 'deferred_missing_rhi_capability',
		coreBehaviorSummary: 'The upstream default is samples=4 and the same multisampled draw writes multiple color attachments that are resolved before the post pass.',
		missingCapability: 'A four-sample render target/pipeline contract and per-attachment multisample resolve usable by the DSL on both Metal and Vulkan.',
		whyCurrentDslCannotExpressIt: 'Current texture creation has no sample-count parameter, RenderPipelineDescriptor has no multisample state, and RenderPassColorAttachment has no resolve target. Replacing the default with samples=0 would change the initial frame semantics; multipass supersampling is not the same MRT renderbuffer/resolve behavior this case exercises.',
		minimumFutureApi: 'Add a DSL-visible sample count on render textures/pipelines plus a color resolve attachment, with matching RHI descriptors and Metal/Vulkan implementations; no case-private escape hatch.',
		repositoryCapabilityEvidence: frozenMultisampleEvidence,
		reentryTest: 'Render the default samples=4 MRT scene, resolve every attachment, verify the post pass reads both resolved outputs, and pass oracle plus cross-quadrant image gates.'
	} );

	addDecision( decisions, 'webgl_multisampled_renderbuffers', 'multisample_resolve', {
		decision: 'deferred',
		reasonCode: 'deferred_missing_rhi_capability',
		coreBehaviorSummary: 'The example renders the same one-hundred-object Scene into adjacent single-sample and four-sample composer targets specifically to demonstrate the resolved edge difference.',
		missingCapability: 'A four-sample offscreen color/depth attachment and resolve contract exposed through DSL and implemented consistently by Metal and Vulkan.',
		whyCurrentDslCannotExpressIt: 'The frozen DSL texture and RenderClass APIs have no sample count, while the RHI render pipeline and pass attachment contracts have neither multisample state nor a resolve target. Replacing the right half with single-sample or generic supersampling would remove the behavior named and compared by the example.',
		minimumFutureApi: 'Add sample count to DSL render textures and pipelines plus resolve attachments to render passes, carry the contract through RHI, and implement matching Metal/Vulkan resolves.',
		repositoryCapabilityEvidence: frozenMultisampleEvidence,
		reentryTest: 'Render deterministic identical geometry in the left single-sample and right four-sample scissor regions, resolve the right target, and pass the fixed Oracle plus all cross-quadrant comparisons.'
	} );

	addDecision( decisions, 'webgl_postprocessing_unreal_bloom_selective', 'multisample_resolve', {
		decision: 'not_core_behavior',
		coreBehaviorSummary: 'The example demonstrates layer-selective Unreal bloom and composition; the final composer happens to use a four-sample target but does not expose or compare multisample behavior.',
		whyNotCoreBehavior: 'Selection, bloom thresholding, blur, additive composition, pointer toggling, and output conversion remain observable without a multisample API. The sample count is incidental edge smoothing rather than the named behavior.',
		existingDslEquivalentPath: 'Keep every Scene and fullscreen pass in DSL, render the final composition through the existing single-sample attachment path, and use deterministic private supersampling/downsampling only if required by the immutable image thresholds.',
		repositoryCapabilityEvidence: singleSampleEvidence,
		reentryTest: 'Capture default and pointer-selected bloom states, verify selective glow and composition, and pass the global image and cross-quadrant gates without claiming multisample support.'
	} );

	addDecision( decisions, 'webgpu_multisampled_renderbuffers', 'multisample_resolve', {
		decision: 'deferred',
		reasonCode: 'deferred_missing_rhi_capability',
		coreBehaviorSummary: 'The example is specifically a multisampled-renderbuffer demonstration and defaults its offscreen target to four samples before presenting the resolved texture.',
		missingCapability: 'A four-sample offscreen color/depth attachment and explicit or automatic resolve contract exposed through DSL and implemented in all four quadrants.',
		whyCurrentDslCannotExpressIt: 'Neither DSL texture creation nor the current RHI render pipeline/pass descriptors carry sample count or resolve attachments. A single-sample or supersampled replacement would not exercise the example\'s core multisampled renderbuffer behavior.',
		minimumFutureApi: 'Introduce sample-count-compatible texture, render pipeline, and resolve attachment fields in the public DSL/RHI contract, followed by Metal/Vulkan resolve support.',
		repositoryCapabilityEvidence: frozenMultisampleEvidence,
		reentryTest: 'Toggle the canonical multisampling state, verify a four-sample target resolves into the sampled quad texture, and pass the global image thresholds in all quadrants.'
	} );

	addDecision( decisions, 'webgl_clipping_stencil', 'complete_stencil_operations_and_reference', {
		decision: 'deferred',
		reasonCode: 'deferred_missing_dsl_capability',
		coreBehaviorSummary: 'The clipping caps depend on increment-wrap/decrement-wrap stencil updates for back/front faces, NotEqual reference testing, Replace operations, and stencil clearing between ordered plane groups.',
		missingCapability: 'Complete DSL stencil compare/reference/read-write masks and fail/depth-fail/pass operations, with matching render-pass stencil load/store/clear semantics.',
		whyCurrentDslCannotExpressIt: 'IRenderClass exposes depth compare/write and cull state but no stencil state. The RHI DepthStencilState and RenderPassDepthStencilAttachment likewise contain no stencil face operations or reference. Color-buffer counting would be a different algorithm and would not validate the core stencil behavior named by this example.',
		minimumFutureApi: 'Add front/back StencilFaceState, stencil reference/read/write masks, and stencil attachment load/store/clear to the public DSL and RHI, then implement the same contract in Metal and Vulkan.',
		repositoryCapabilityEvidence: frozenStencilEvidence,
		reentryTest: 'Exercise all three clipping planes with increment/decrement wrap, NotEqual ref=0, Replace, and per-plane clear; compare caps and ordered geometry in all quadrants.'
	} );

	addDecision( decisions, 'webgpu_occlusion', 'occlusion_query', {
		decision: 'deferred',
		reasonCode: 'deferred_missing_rhi_capability',
		coreBehaviorSummary: 'The test object enables occlusionTest and renderer.isOccluded asynchronously drives the plane color when the sphere is completely hidden.',
		missingCapability: 'Per-renderable occlusion query begin/end, result resolve, and availability semantics exposed to DSL/host across Metal and Vulkan.',
		whyCurrentDslCannotExpressIt: 'The RHI QueryType enum only exposes timestamp and pass-counter queries, and no RenderClass/pass API brackets an entity draw with an occlusion query. A custom fragment atomic visibility pass would change the core query API behavior demonstrated by the example.',
		minimumFutureApi: 'Add an Occlusion QueryType, render-pass/entity query bracketing, resolve/availability results, and a DSL/host access path with consistent frame-latency semantics.',
		repositoryCapabilityEvidence: [ ...frozenOcclusionEvidence, ...fragmentAtomicEvidence ],
		reentryTest: 'Move the sphere through visible, partially occluded, and completely occluded canonical states; verify query availability/latency and the plane color transition in all quadrants.'
	} );

	addDecision( decisions, 'webgpu_postprocessing_godrays', 'blend_constant', {
		decision: 'not_core_behavior',
		coreBehaviorSummary: 'blendColor is a shader uniform passed to depthAwareBlend; it is not a fixed-function constant blend factor or encoder blend constant.',
		whyNotCoreBehavior: 'The token detector matched the local variable name. Both cited call sites perform shader-node compositing into the postprocess output.',
		existingDslEquivalentPath: 'Store the color in an existing uniform buffer and execute depthAwareBlend as a fullscreen DSL fragment pass with ordinary shader arithmetic.',
		repositoryCapabilityEvidence: [
			{
				path: 'GVM/UGLHeaders/Details/UGL.Resources.h',
				line: 264,
				excerpt: 'class UniformBuffer'
			},
			{
				path: 'GVM/UGLHeaders/Details/UGL.Shaders.h',
				line: 130,
				excerpt: 'RenderPassTaskDescriptor operator()(uint vertexCount, uint instanceCount, uint firstVertex, uint firstInstance)'
			}
		],
		reentryTest: 'Change blendColor to a fixed non-default value and compare the fullscreen god-rays composite without any Constant blend factor in generated pipeline state.'
	} );

	return decisions;

}

/** Creates stable aggregate counts for the generated adjudication document. */
function buildCounts( generatedDecisions ) {

	const byDecision = {
		deferred: 0,
		expressible_existing_dsl: 0,
		not_core_behavior: 0,
		needs_further_review: 0
	};
	const byCapability = {};
	const candidateExamples = new Set();
	const deferredExamples = new Set();

	for ( const decision of generatedDecisions ) {

		byDecision[ decision.decision ] ++;
		candidateExamples.add( decision.exampleId );
		if ( decision.decision === 'deferred' ) deferredExamples.add( decision.exampleId );
		byCapability[ decision.capability ] ??= {
			candidateCount: 0,
			deferred: 0,
			expressible_existing_dsl: 0,
			not_core_behavior: 0,
			needs_further_review: 0
		};
		byCapability[ decision.capability ].candidateCount ++;
		byCapability[ decision.capability ][ decision.decision ] ++;

	}

	return {
		candidateExampleCount: candidateExamples.size,
		decisionCount: generatedDecisions.length,
		deferredExampleCount: deferredExamples.size,
		byDecision,
		byCapability
	};

}

/** Builds the deterministic standalone adjudication document from the audit evidence and manual decisions. */
export function buildCapabilityAdjudication( audit, freeze ) {

	if ( audit.upstream.commit !== expectedCommit ) throw new Error( `Capability audit must use Three.js commit ${ expectedCommit }.` );
	const manualDecisions = buildManualDecisionMap();
	const generatedDecisions = [];

	for ( const example of audit.examples ) {

		for ( const candidate of example.missingCapabilityCandidates ) {

			const key = decisionKey( example.id, candidate.capability );
			const manualDecision = manualDecisions.get( key );
			if ( ! manualDecision ) throw new Error( `Missing manual adjudication for ${ key }.` );
			manualDecisions.delete( key );
			generatedDecisions.push( {
				exampleId: example.id,
				upstreamPath: example.upstreamPath,
				capability: candidate.capability,
				decision: manualDecision.decision,
				coreBehaviorSummary: manualDecision.coreBehaviorSummary,
				upstreamSourceEvidence: [ ...candidate.evidence, ...( supplementalUpstreamEvidence.get( key ) ?? [] ) ],
				...( manualDecision.whyNotCoreBehavior ? { whyNotCoreBehavior: manualDecision.whyNotCoreBehavior } : {} ),
				...( manualDecision.existingDslEquivalentPath ? { existingDslEquivalentPath: manualDecision.existingDslEquivalentPath } : {} ),
				...( manualDecision.reasonCode ? { reasonCode: manualDecision.reasonCode } : {} ),
				...( manualDecision.missingCapability ? { missingCapability: manualDecision.missingCapability } : {} ),
				...( manualDecision.whyCurrentDslCannotExpressIt ? { whyCurrentDslCannotExpressIt: manualDecision.whyCurrentDslCannotExpressIt } : {} ),
				...( manualDecision.minimumFutureApi ? { minimumFutureApi: manualDecision.minimumFutureApi } : {} ),
				repositoryCapabilityEvidence: manualDecision.repositoryCapabilityEvidence,
				reentryTest: manualDecision.reentryTest,
				manifestMutationAllowed: false
			} );

		}

	}

	if ( manualDecisions.size !== 0 ) {

		throw new Error( `Manual adjudication contains ${ manualDecisions.size } pair(s) absent from the audit: ${ [ ...manualDecisions.keys() ].join( ', ' ) }.` );

	}

	const counts = buildCounts( generatedDecisions );
	return {
		schemaVersion: 1,
		phase: 'three-r185-phase1',
		upstream: audit.upstream,
		inputs: {
			capabilityAudit: 'GVMRuntime_ThreeSamples/Manifest/three-r185-capability-audit.json',
			capabilityFreeze: 'GVMRuntime_ThreeSamples/Manifest/phase1-capability-freeze.json',
			capabilityFreezeSchemaVersion: freeze.schemaVersion
		},
		policy: {
			manifestMutationAllowed: false,
			publicCapabilityExpansionAllowed: false,
			adjudicationUnit: 'example_capability_pair',
			allowedDecisions: [ 'deferred', 'expressible_existing_dsl', 'not_core_behavior', 'needs_further_review' ],
			deferredReasonCodes: freeze.deferredReasonCodes,
			note: 'This file adjudicates only detector candidates. It does not classify the example against capabilities outside the candidate pair.'
		},
		counts,
		decisions: generatedDecisions
	};

}

/** Serializes the adjudication document with stable indentation and a terminal newline. */
function serializeAdjudication( document ) {

	return `${ JSON.stringify( document, null, 2 ) }\n`;

}

/** Runs generation or deterministic --check mode from explicit CLI arguments. */
function main() {

	const auditPath = readPathOption( process.argv.slice( 2 ), '--audit', defaultAuditPath );
	const freezePath = readPathOption( process.argv.slice( 2 ), '--freeze', defaultFreezePath );
	const outputPath = readPathOption( process.argv.slice( 2 ), '--output', defaultOutputPath );
	const checkOnly = process.argv.includes( '--check' );
	const audit = JSON.parse( fs.readFileSync( auditPath, 'utf8' ) );
	const freeze = JSON.parse( fs.readFileSync( freezePath, 'utf8' ) );
	const serialized = serializeAdjudication( buildCapabilityAdjudication( audit, freeze ) );

	if ( checkOnly ) {

		if ( ! fs.existsSync( outputPath ) || fs.readFileSync( outputPath, 'utf8' ) !== serialized ) {

			throw new Error( `Capability adjudication is stale: ${ path.relative( repositoryRoot, outputPath ) }.` );

		}
		console.log( `Capability adjudication is deterministic: ${ path.relative( repositoryRoot, outputPath ) }.` );
		return;

	}

	fs.writeFileSync( outputPath, serialized );
	console.log( `Wrote ${ path.relative( repositoryRoot, outputPath ) }.` );

}

if ( process.argv[ 1 ] && path.resolve( process.argv[ 1 ] ) === fileURLToPath( import.meta.url ) ) main();
