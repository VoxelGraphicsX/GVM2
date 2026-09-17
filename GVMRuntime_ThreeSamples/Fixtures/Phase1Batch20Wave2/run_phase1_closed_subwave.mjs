#!/usr/bin/env node

import { existsSync, promises as fs, readFileSync } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath, pathToFileURL } from 'node:url';

import {
  buildCrossComparisons,
  buildStabilityComparisons,
  requiredBackends,
  requiredPipelines,
  runQuadrant,
  writeJson
} from '../../../tests/runners/three/node/runner.mjs';
import { lintSampleGpuBoundary } from '../../Tools/lint_sample_gpu_boundary.mjs';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, '../../..');
export const caseDefinitions = Object.freeze([
  {
    caseId: 'webgpu_materials_transmission',
    shardName: 'WebgpuMaterialsTransmission',
    passes: [
      'WebgpuMaterialsTransmissionEnvironmentBackgroundPass',
      'WebgpuMaterialsTransmissionScreenMipPass',
      'WebgpuMaterialsTransmissionFrontPass',
      'WebgpuMaterialsTransmissionBackPass',
      'WebgpuMaterialsTransmissionOutputToneMapPass'
    ]
  },
  {
    caseId: 'webgpu_parallax_uv',
    shardName: 'WebgpuParallaxUv',
    passes: [
      'WebgpuParallaxUvBackgroundPass',
      'WebgpuParallaxUvScenePass',
      'WebgpuParallaxUvOutputPass'
    ]
  },
  {
    caseId: 'webgl_interactive_points',
    manifestShard: 'Phase1InteractiveSimple',
    shardName: 'WebglInteractivePoints',
    passes: ['WebglInteractivePointsMainPass']
  },
  {
    caseId: 'webgl_points_sprites',
    manifestShard: 'Phase1LinesPointsRenderSet',
    shardName: 'WebglPointsSprites',
    passes: ['WebglPointsSpritesAdditivePass']
  },
  {
    caseId: 'webgpu_camera',
    manifestShard: 'Phase1CameraRenderSet',
    shardName: 'WebgpuCamera',
    passes: [
      'WebgpuCameraActiveCameraPass',
      'WebgpuCameraObserverCameraPass'
    ]
  },
  {
    caseId: 'webgl_camera',
    manifestShard: 'Phase1CameraRenderSet',
    shardName: 'WebglCamera',
    passes: [
      'WebglCameraActiveCameraPass',
      'WebglCameraObserverCameraPass'
    ]
  },
  {
    caseId: 'webgpu_lightprobe',
    shardName: 'WebgpuLightprobe',
    passes: [
      'WebgpuLightprobeBackgroundPass',
      'WebgpuLightprobeSceneMainPass'
    ],
    singleSampleManifestOverride: true
  },
  {
    caseId: 'webgl_lightprobe',
    shardName: 'WebglLightprobe',
    passes: [
      'WebglLightprobeBackgroundPass',
      'WebglLightprobeSceneMainPass'
    ],
    singleSampleManifestOverride: true
  },
  {
    caseId: 'webgpu_lightprobe_cubecamera',
    shardName: 'WebgpuLightprobeCubecamera',
    passes: [
      'WebgpuLightprobeCubecameraBackgroundPass',
      'WebgpuLightprobeCubecameraSceneMainPass',
      'WebgpuLightprobeCubecameraInspectorPass'
    ],
    singleSampleManifestOverride: true
  },
  {
    caseId: 'webgl_lightprobe_cubecamera',
    shardName: 'WebglLightprobeCubecamera',
    passes: [
      'WebglLightprobeCubecameraBackgroundPass',
      'WebglLightprobeCubecameraSceneMainPass'
    ],
    singleSampleManifestOverride: true
  },
  {
    caseId: 'webgpu_postprocessing',
    manifestShard: 'Phase1WebgpuPostprocessingRenderSet',
    shardName: 'WebgpuPostprocessing',
    passes: [
      'WebgpuPostprocessingMainPass',
      'WebgpuPostprocessingDotScreenPass',
      'WebgpuPostprocessingRgbShiftPass',
      'WebgpuPostprocessingOutputPass'
    ]
  },
  {
    caseId: 'webgl_postprocessing',
    manifestShard: 'Phase1WebglPostprocessingRenderSet',
    shardName: 'WebglPostprocessing',
    passes: [
      'WebglPostprocessingMainPass',
      'WebglPostprocessingDotScreenPass',
      'WebglPostprocessingRgbShiftPass',
      'WebglPostprocessingOutputPass'
    ]
  },
  {
    caseId: 'webgpu_postprocessing_masking',
    manifestShard: 'Phase1WebgpuPostprocessingMaskingRenderSet',
    shardName: 'WebgpuPostprocessingMasking',
    passes: [
      'WebgpuPostprocessingMaskingBasePass',
      'WebgpuPostprocessingMaskingBoxPass',
      'WebgpuPostprocessingMaskingTorusPass'
    ]
  },
  {
    caseId: 'webgl_postprocessing_masking',
    manifestShard: 'Phase1WebglPostprocessingMaskingRenderSet',
    shardName: 'WebglPostprocessingMasking',
    passes: [
      'WebglPostprocessingMaskingScene1MaskPass',
      'WebglPostprocessingMaskingScene2MaskPass',
      'WebglPostprocessingMaskingCompositePass'
    ]
  },
  {
    caseId: 'webgl_test_memory',
    manifestShard: 'Phase1WebglTestMemoryRenderSet',
    shardName: 'WebglTestMemory',
    passes: ['WebglTestMemoryScenePass']
  },
  {
    caseId: 'webgl_shader',
    shardName: 'WebglShader',
    passes: ['WebglShaderMonjoriPass']
  },
  {
    caseId: 'webgl_buffergeometry_attributes_none',
    shardName: 'WebglBuffergeometryAttributesNone',
    passes: ['WebglBuffergeometryAttributesNoneScenePass'],
    singleSampleManifestOverride: true
  },
  {
    caseId: 'misc_animation_keys',
    manifestShard: 'Phase1MiscAnimationKeysRenderSet',
    shardName: 'MiscAnimationKeys',
    passes: [
      'MiscAnimationKeysAxesPass',
      'MiscAnimationKeysBoxPass'
    ]
  },
  {
    caseId: 'misc_animation_groups',
    manifestShard: 'Phase1MiscAnimationGroupsRenderSet',
    shardName: 'MiscAnimationGroups',
    passes: ['MiscAnimationGroupsMainPass']
  },
  {
    caseId: 'webgl_sprites',
    manifestShard: 'Phase1LinesPointsRenderSet',
    shardName: 'WebglSprites',
    passes: [
      'WebglSpritesWorldPass',
      'WebglSpritesHudPass',
      'WebglSpritesOutputPass'
    ]
  },
  {
    caseId: 'webgpu_sprites',
    manifestShard: 'Phase1WebgpuSpritesRenderSet',
    shardName: 'WebgpuSprites',
    passes: ['WebgpuSpritesScenePass', 'WebgpuSpritesOutputPass'],
    singleSampleManifestOverride: true
  },
  {
    caseId: 'webgl_buffergeometry_lines_indexed',
    shardName: 'WebglBuffergeometryLinesIndexed',
    passes: ['WebglBuffergeometryLinesIndexedMainPass']
  },
  {
    caseId: 'webgl_buffergeometry_custom_attributes_particles',
    shardName: 'WebglBuffergeometryCustomAttributesParticles',
    passes: ['WebglBuffergeometryCustomAttributesParticlesMainPass']
  },
  {
    caseId: 'webgl_buffergeometry_instancing_billboards',
    shardName: 'WebglBuffergeometryInstancingBillboards',
    passes: ['WebglBuffergeometryInstancingBillboardsMainPass']
  },
  {
    caseId: 'webgl_instancing_performance',
    shardName: 'WebglInstancingPerformance',
    passes: [
      'WebglInstancingPerformanceMainPass',
      'WebglInstancingPerformanceResolvePass'
    ]
  },
  {
    caseId: 'webgpu_compute_particles',
    shardName: 'WebgpuComputeParticles',
    passes: [
      'WebgpuComputeParticlesGridPass',
      'WebgpuComputeParticlesSpritePass',
      'WebgpuComputeParticlesInspectorPass'
    ],
    computePasses: [
      'WebgpuComputeParticlesInitPass',
      'WebgpuComputeParticlesHitPass',
      'WebgpuComputeParticlesUpdatePass'
    ]
  },
  {
    caseId: 'webgpu_compute_points',
    shardName: 'WebgpuComputePoints',
    passes: [
      'WebgpuComputePointsMainPass',
      'WebgpuComputePointsResolvePass',
      'WebgpuComputePointsRasterConventionPass',
      'WebgpuComputePointsInspectorPass'
    ],
    computePasses: ['WebgpuComputePointsUpdatePass']
  },
  {
    caseId: 'webgpu_compute_geometry',
    shardName: 'WebgpuComputeGeometry',
    passes: [
      'WebgpuComputeGeometryMainPass',
      'WebgpuComputeGeometryBackgroundPass',
      'WebgpuComputeGeometryInspectorPass',
      'WebgpuComputeGeometryResolvePass'
    ],
    computePasses: [
      'WebgpuComputeGeometryInitPass',
      'WebgpuComputeGeometryUpdatePass'
    ]
  },
  {
    caseId: 'webgpu_compute_birds',
    manifestShard: 'Phase1WebgpuComputeRenderSet',
    shardName: 'WebgpuComputeBirds',
    passes: [
      'WebgpuComputeBirdsSkyPass',
      'WebgpuComputeBirdsFlockPass',
      'WebgpuComputeBirdsNeutralOutputPass',
      'WebgpuComputeBirdsInspectorPass'
    ],
    computePasses: [
      'WebgpuComputeBirdsInitPass',
      'WebgpuComputeBirdsVelocityPass',
      'WebgpuComputeBirdsPositionPass'
    ]
  },
  {
    caseId: 'webgpu_instance_mesh',
    shardName: 'WebgpuInstanceMesh',
    passes: [
      'WebgpuInstanceMeshMainPass',
      'WebgpuInstanceMeshInspectorPass'
    ]
  },
  {
    caseId: 'webgpu_instance_points',
    shardName: 'WebgpuInstancePoints',
    passes: [
      'WebgpuInstancePointsMainPass',
      'WebgpuInstancePointsInsetBackgroundPass',
      'WebgpuInstancePointsInsetPass',
      'WebgpuInstancePointsInspectorPass',
      'WebgpuInstancePointsResolvePass'
    ],
    computePasses: ['WebgpuInstancePointsSizeComputePass']
  },
  {
    caseId: 'webgpu_multiple_rendertargets',
    shardName: 'WebgpuMultipleRendertargets',
    passes: [
      'WebgpuMultipleRendertargetsScenePass',
      'WebgpuMultipleRendertargetsCompositePass'
    ]
  },
  {
    caseId: 'webgpu_postprocessing_radial_blur',
    manifestShard: 'Phase1WebgpuPostprocessingRadialBlurRenderSet',
    shardName: 'Phase1WebgpuPostprocessingRadialBlurRenderSet',
    passes: [
      'WebgpuPostprocessingRadialBlurMainPass',
      'WebgpuPostprocessingRadialBlurCompositePass'
    ],
    singleSampleManifestOverride: true
  },
  {
    caseId: 'webgl_texture2darray_layerupdate',
    manifestShard: 'Phase1WebglTexture2DArrayLayerUpdateRenderSet',
    shardName: 'WebglTexture2DArrayLayerUpdate',
    passes: ['WebglTexture2DArrayLayerUpdateScenePass']
  },
  {
    caseId: 'webgl_loader_texture_hdr',
    shardName: 'Phase1LoaderTextureHdrSimple',
    passes: ['WebglLoaderTextureHdrQuadPass']
  },
  {
    caseId: 'webgl_loader_vox',
    shardName: 'Phase1LoaderVoxSimple',
    passes: ['WebglLoaderVoxMeshPass']
  },
  {
    caseId: 'webgl_postprocessing_fxaa',
    shardName: 'Phase1WebglPostprocessingFxaaRenderSet',
    passes: [
      'WebglPostprocessingFxaaMainPass',
      'WebglPostprocessingFxaaOutputColorPass',
      'WebglPostprocessingFxaaCompositePass'
    ]
  },
  {
    caseId: 'webgl_camera_array',
    manifestShard: 'Phase1CameraRenderSet',
    shardName: 'Phase1WebglCameraArrayRenderSet',
    passes: [
      'WebglCameraArrayShadowDepthPass',
      'WebglCameraArrayMainPass'
    ]
  },
  {
    caseId: 'webgpu_camera_array',
    manifestShard: 'Phase1CameraRenderSet',
    shardName: 'Phase1WebgpuCameraArrayRenderSet',
    passes: [
      'WebgpuCameraArrayShadowDepthPass',
      'WebgpuCameraArrayMainPass'
    ]
  },
  {
    caseId: 'webgl_materials_envmaps',
    manifestShard: 'Phase1MaterialsSimple',
    shardName: 'Phase1WebglMaterialsEnvmaps',
    passes: [
      'WebglMaterialsEnvmapsEnvironmentBackgroundPass',
      'WebglMaterialsEnvmapsMainPass',
      'WebglMaterialsEnvmapsOutputToneMapPass'
    ],
    computePasses: ['WebglMaterialsEnvmapsCubeAtlasGutterBuild']
  },
  {
    caseId: 'webgpu_materials_envmaps',
    manifestShard: 'Phase1WebgpuMaterialsSimple',
    shardName: 'WebgpuMaterialsEnvmaps',
    passes: [
      'WebgpuMaterialsEnvmapsEnvironmentBackgroundPass',
      'WebgpuMaterialsEnvmapsMainPass',
      'WebgpuMaterialsEnvmapsOutputToneMapPass'
    ],
    computePasses: [
      'WebgpuMaterialsEnvmapsCubeAtlasGutterBuild',
      'WebgpuMaterialsEnvmapsEquirectangularMipBuild'
    ]
  },
  {
    caseId: 'webgpu_materials_displacementmap',
    manifestShard: 'Phase1WebgpuMaterialsSimple',
    shardName: 'WebgpuMaterialsDisplacementmap',
    passes: [
      'WebgpuMaterialsDisplacementmapBackPass',
      'WebgpuMaterialsDisplacementmapFrontPass'
    ]
  },
  {
    caseId: 'webgl_materials_cubemap',
    manifestShard: 'Phase1MaterialsRenderSet',
    shardName: 'WebglMaterialsCubemap',
    passes: [
      'WebglMaterialsCubemapBackgroundPass',
      'WebglMaterialsCubemapMainPass'
    ]
  },
  {
    caseId: 'webgl_geometries',
    manifestShard: 'Phase1WebglGeometriesRenderSet',
    shardName: 'WebglGeometries',
    passes: [
      'WebglGeometriesBackPass',
      'WebglGeometriesFrontPass'
    ]
  },
  {
    caseId: 'webgl_materials_normalmap_object_space',
    shardName: 'WebglMaterialsNormalmapObjectSpace',
    passes: [
      'WebglMaterialsNormalmapObjectSpaceBackFacesPass',
      'WebglMaterialsNormalmapObjectSpaceFrontFacesPass'
    ]
  },
  {
    caseId: 'webgl_materials_normalmap',
    shardName: 'WebglMaterialsNormalmap',
    manifestShard: 'Phase1MaterialsSimple',
    passes: [
      'WebglMaterialsNormalmapMainPass',
      'WebglMaterialsNormalmapBleachPass',
      'WebglMaterialsNormalmapColorPass',
      'WebglMaterialsNormalmapOutputPass',
      'WebglMaterialsNormalmapFxaaPass'
    ]
  },
  {
    caseId: 'webgl_materials_matcap',
    shardName: 'WebglMaterialsMatcap',
    passes: ['WebglMaterialsMatcapMainPass']
  },
  {
    caseId: 'webgl_geometry_extrude_splines',
    shardName: 'WebglGeometryExtrudeSplines',
    passes: [
      'WebglGeometryExtrudeSplinesOpaquePass',
      'WebglGeometryExtrudeSplinesTransparentWireframePass'
    ]
  },
  {
    caseId: 'webgl_geometry_teapot',
    shardName: 'WebglGeometryTeapot',
    passes: [
      'WebglGeometryTeapotTeapotPass',
      'WebglGeometryTeapotCubeBackgroundPass'
    ]
  },
  {
    caseId: 'webgl_lines_dashed',
    shardName: 'Phase1LinesPointsRenderSet',
    passes: ['WebglLinesDashedMainPass']
  },
  {
    caseId: 'webgl_points_billboards',
    shardName: 'Phase1LinesPointsSimple',
    passes: ['WebglPointsBillboardsMainPass']
  },
  {
    caseId: 'webgl_geometry_convex',
    shardName: 'WebglGeometryConvex',
    passes: [
      'WebglGeometryConvexOpaquePass',
      'WebglGeometryConvexAxisPass',
      'WebglGeometryConvexTransparentHullPass'
    ]
  },
  {
    caseId: 'webgl_geometry_nurbs',
    shardName: 'WebglGeometryNurbs',
    passes: [
      'WebglGeometryNurbsOpaquePass',
      'WebglGeometryNurbsTransparentControlLinePass'
    ]
  },
  {
    caseId: 'webgl_geometry_terrain',
    shardName: 'WebglGeometryTerrain',
    passes: ['WebglGeometryTerrainMainPass'],
    computePasses: ['WebglGeometryTerrainTexturePass']
  },
  {
    caseId: 'webgpu_tsl_raging_sea',
    shardName: 'WebgpuTslRagingSea',
    manifestShard: 'Phase1WebgpuTslRagingSea',
    passes: [
      'WebgpuTslRagingSeaMainPass',
      'WebgpuTslRagingSeaOutputPass'
    ]
  },
  {
    caseId: 'webgpu_compute_reduce',
    shardName: 'Phase1WebgpuComputeReduceSimple',
    passes: [
      'WebgpuComputeReduceLeftPass',
      'WebgpuComputeReduceRightPass'
    ],
    computePasses: [
      'WebgpuComputeReduceResetPass',
      'WebgpuComputeReduceNOverTwoPass',
      'WebgpuComputeReduceWorkgroupPass',
      'WebgpuComputeReduceSubgroupPass',
      'WebgpuComputeReduceFinalizePass'
    ]
  },
  {
    caseId: 'webgl_morphtargets_horse',
    shardName: 'WebglMorphtargetsHorse',
    passes: ['WebglMorphtargetsHorseMainPass']
  },
  {
    caseId: 'webgl_geometry_colors_lookuptable',
    shardName: 'WebglGeometryColorsLookuptable',
    passes: [
      'WebglGeometryColorsLookuptableMainPass',
      'WebglGeometryColorsLookuptableLegendPass'
    ],
    computePasses: ['WebglGeometryColorsLookuptableLegendComputePass']
  },
  {
    caseId: 'webgl_shader_lava',
    shardName: 'WebglShaderLava',
    passes: [
      'WebglShaderLavaScenePass',
      'WebglShaderLavaBloomHorizontalPass',
      'WebglShaderLavaBloomVerticalPass',
      'WebglShaderLavaBloomCombinePass',
      'WebglShaderLavaOutputPass'
    ]
  },
  {
    caseId: 'webgl_materials_bumpmap',
    manifestShard: 'Phase1MaterialsSimple',
    shardName: 'WebglMaterialsBumpmap',
    passes: [
      'WebglMaterialsBumpmapShadowDepthPass',
      'WebglMaterialsBumpmapMainPass'
    ]
  },
  {
    caseId: 'webgpu_multiple_rendertargets_readback',
    shardName: 'WebgpuMultipleRendertargetsReadback',
    passes: [
      'WebgpuMultipleRendertargetsReadbackScenePass',
      'WebgpuMultipleRendertargetsReadbackByteScenePass',
      'WebgpuMultipleRendertargetsReadbackCompositePass',
      'WebgpuMultipleRendertargetsReadbackQuantizePass'
    ]
  },
  {
    caseId: 'webgl_buffergeometry',
    shardName: 'WebglBuffergeometry',
    randomSeed: 0x18500015,
    passes: [
      'WebglBuffergeometryBackSidePass',
      'WebglBuffergeometryFrontSidePass'
    ]
  },
  {
    caseId: 'webgl_buffergeometry_attributes_integer',
    shardName: 'WebglBuffergeometryAttributesInteger',
    randomSeed: 0x18500001,
    passes: ['WebglBuffergeometryAttributesIntegerMainPass']
  },
  {
    caseId: 'webgl_buffergeometry_indexed',
    shardName: 'WebglBuffergeometryIndexed',
    randomSeed: 0x18500005,
    passes: [
      'WebglBuffergeometryIndexedFilledPass',
      'WebglBuffergeometryIndexedWireframePass'
    ]
  },
  {
    caseId: 'webgl_buffergeometry_instancing',
    shardName: 'WebglBuffergeometryInstancing',
    randomSeed: 0x18500006,
    passes: ['WebglBuffergeometryInstancingMainPass']
  },
  {
    caseId: 'webgl_buffergeometry_lines',
    shardName: 'WebglBuffergeometryLines',
    passes: ['WebglBuffergeometryLinesMainPass']
  },
  {
    caseId: 'webgl_buffergeometry_rawshader',
    shardName: 'WebglBuffergeometryRawshader',
    passes: ['WebglBuffergeometryRawshaderMainPass']
  },
  {
    caseId: 'webgl_buffergeometry_selective_draw',
    shardName: 'WebglBuffergeometrySelectiveDraw',
    randomSeed: 0x1850000e,
    passes: ['WebglBuffergeometrySelectiveDrawMainPass']
  },
  {
    caseId: 'webgl_buffergeometry_drawrange',
    shardName: 'WebglBuffergeometryDrawrange',
    randomSeed: 407896067,
    passes: [
      'WebglBuffergeometryDrawrangeBoxLinePass',
      'WebglBuffergeometryDrawrangeMainPass',
      'WebglBuffergeometryDrawrangeNativeLinePass'
    ]
  },
  {
    caseId: 'webgl_buffergeometry_uint',
    shardName: 'WebglBuffergeometryUint',
    randomSeed: 0x1850000f,
    passes: ['WebglBuffergeometryUintMainPass']
  },
  {
    caseId: 'webgl_custom_attributes',
    shardName: 'WebglCustomAttributes',
    randomSeed: 0x18500010,
    passes: ['WebglCustomAttributesMainPass']
  },
  {
    caseId: 'webgl_custom_attributes_points',
    shardName: 'WebglCustomAttributesPoints',
    randomSeed: 0x18500012,
    passes: ['WebglCustomAttributesPointsMainPass']
  },
  {
    caseId: 'webgl_custom_attributes_points2',
    shardName: 'WebglCustomAttributesPoints2',
    randomSeed: 0x18500013,
    passes: ['WebglCustomAttributesPoints2MainPass']
  },
  {
    caseId: 'webgl_custom_attributes_points3',
    shardName: 'WebglCustomAttributesPoints3',
    randomSeed: 0x18500014,
    passes: ['WebglCustomAttributesPoints3MainPass']
  },
  {
    caseId: 'webgl_geometry_cube',
    shardName: 'WebglGeometryCube',
    passes: ['WebglGeometryCubeScenePass']
  },
  {
    caseId: 'webgl_instancing_raycast',
    shardName: 'WebglInstancingRaycast',
    passes: ['WebglInstancingRaycastMainPass']
  },
  {
    caseId: 'webgl_materials_texture_canvas',
    shardName: 'WebglMaterialsTextureCanvas',
    passes: ['WebglMaterialsTextureCanvasScenePass']
  },
  {
    caseId: 'webgl_materials_texture_partialupdate',
    shardName: 'WebglMaterialsTexturePartialupdate',
    randomSeed: 42,
    passes: ['WebglMaterialsTexturePartialupdateMainPass'],
    computePasses: [
      'WebglMaterialsTexturePartialupdateBaseCopyPass',
      'WebglMaterialsTexturePartialupdatePatchPass'
    ]
  },
  {
    caseId: 'webgl_materials_texture_rotation',
    shardName: 'WebglMaterialsTextureRotation',
    passes: ['WebglMaterialsTextureRotationMainPass']
  },
  {
    caseId: 'webgl_postprocessing_procedural',
    manifestShard: 'Phase1WebglPostprocessingProcedural',
    shardName: 'Phase1WebglPostprocessingProcedural',
    passes: [
      'WebglPostprocessingProceduralCoordinatePass',
      'WebglPostprocessingProceduralPass'
    ]
  },
  {
    caseId: 'webgl_buffergeometry_instancing_interleaved',
    shardName: 'WebglBuffergeometryInstancingInterleaved',
    passes: ['WebglBuffergeometryInstancingInterleavedMainPass']
  },
  {
    caseId: 'webgl_loader_xyz',
    shardName: 'Phase1LoaderXyzSimple',
    passes: ['WebglLoaderXyzPointPass']
  }
]);

/** Parses unique long-form options and rejects implicit runtime configuration. */
export function parseArguments(argv) {
  const options = {};
  for (let index = 2; index < argv.length; index += 2) {
    const option = argv[index];
    const value = argv[index + 1];
    if (!option?.startsWith('--') || value == null || value.startsWith('--')) {
      throw new Error(`Expected --option value pair near '${option ?? '<end>'}'.`);
    }
    const name = option.slice(2);
    if (Object.hasOwn(options, name)) throw new Error(`Duplicate --${name}.`);
    options[name] = value;
  }
  return options;
}

/** Resolves one mandatory explicit path option. */
function requirePath(options, name) {
  if (!options[name]) throw new Error(`Missing required --${name}.`);
  return path.resolve(options[name]);
}

/** Loads and validates the internally closed examples from the frozen 588-item Manifest. */
async function loadExamples(manifestPath, definitions) {
  const manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8'));
  if (!Array.isArray(manifest.examples) || manifest.examples.length !== 588) {
    throw new Error('Batch-20 closed subwave requires the frozen 588-item Three r185 Manifest.');
  }
  return definitions.map((definition) => {
    const example = manifest.examples.find((entry) => entry.id === definition.caseId);
    if (example?.status !== 'phase1_required'
        || example.dslShard !== (definition.manifestShard ?? definition.shardName)
        || !['required', 'not-required'].includes(example.renderSetPolicy)
        || example.scenarios?.length < 2) {
      throw new Error(`${definition.caseId}: Manifest ownership or scenario contract drifted.`);
    }
    const exampleWithDefinitionSeed = definition.randomSeed == null
      ? example
      : { ...example, randomSeed: definition.randomSeed };
    if (!definition.singleSampleManifestOverride) return exampleWithDefinitionSeed;
    return {
      ...exampleWithDefinitionSeed,
      scenarios: exampleWithDefinitionSeed.scenarios.map((scenario) => ({
        ...scenario,
        scenePassInvocations: scenario.scenePassInvocations.map((invocation) => ({
          ...invocation,
          invocationCount: 1
        })),
        scenePassSequence: scenario.scenePassSequence.slice(0, 1)
      }))
    };
  });
}

/** Verifies dual generated headers and every Experimental UGLIR/MSL/SPIR-V stage. */
export function lintGeneratedArtifacts(generatedRoot, definitions = caseDefinitions) {
  const failures = [];
  for (const definition of definitions) {
    for (const pipeline of requiredPipelines) {
      const directory = path.join(generatedRoot, pipeline, definition.shardName, 'UGLBin');
      const generatedPath = path.join(directory, 'generate_result.hpp');
      const dslPath = path.join(directory, 'dsl_single_header.hpp');
      if (!existsSync(generatedPath) || !existsSync(dslPath)) {
        failures.push(`${definition.caseId}/${pipeline}: missing generated headers.`);
        continue;
      }
      const generated = readFileSync(generatedPath, 'utf8');
      const dsl = readFileSync(dslPath, 'utf8');
      for (const passName of definition.passes) {
        if (!generated.includes(passName) || !dsl.includes(passName)) {
          failures.push(`${definition.caseId}/${pipeline}: missing ${passName}.`);
        }
        if (pipeline === 'experimental') {
          for (const stage of ['vertex', 'fragment']) {
            const stem = `${passName}__${stage}`;
            for (const relativePath of [
              `uglir/${stem}.uglir.json`,
              `msl/${stem}.msl`,
              `spv/${stem}.raw.spvasm`,
              `spv/${stem}.spvasm`
            ]) {
              if (!existsSync(path.join(directory, relativePath))) {
                failures.push(`${definition.caseId}: missing Experimental ${relativePath}.`);
              }
            }
          }
        }
      }
      for (const passName of definition.computePasses ?? []) {
        if (!generated.includes(passName) || !dsl.includes(passName)) {
          failures.push(`${definition.caseId}/${pipeline}: missing ${passName}.`);
        }
        if (pipeline === 'experimental') {
          for (const relativePath of [
            `uglir/${passName}.uglir.json`,
            `msl/${passName}.msl`,
            `spv/${passName}.raw.spvasm`,
            `spv/${passName}.spvasm`
          ]) {
            if (!existsSync(path.join(directory, relativePath))) {
              failures.push(`${definition.caseId}: missing Experimental ${relativePath}.`);
            }
          }
        }
      }
      if (dsl.includes('Phase1BatchSimpleRenderer')
          || dsl.includes('Phase1BatchRenderSetRenderer')) {
        failures.push(`${definition.caseId}/${pipeline}: shared placeholder renderer leaked.`);
      }
    }
  }
  return {
    status: failures.length === 0 ? 'pass' : 'fail',
    checkedCases: definitions.map((definition) => definition.caseId),
    failures
  };
}

/** Runs every closed subwave scenario through four quadrants and three repetitions. */
async function runMatrix(context, examples, repetitions) {
  const quadrants = [];
  for (let repetition = 1; repetition <= repetitions; repetition += 1) {
    for (const example of examples) {
      const definition = context.definitions.find((entry) => entry.caseId === example.id);
      context.profile.hostCandidates = {
        legacy: [path.join(context.binaryRoot, `${definition.shardName}-legacy`)],
        experimental: [path.join(context.binaryRoot, `${definition.shardName}-experimental`)]
      };
      for (const scenario of example.scenarios) {
        for (const pipeline of requiredPipelines) {
          for (const backend of requiredBackends) {
            const result = await runQuadrant(
              context, example, scenario, pipeline, backend, repetition);
            quadrants.push(result);
            console.log(
              `${result.status.toUpperCase()} ${example.id}/${scenario.id} `
              + `${pipeline}/${backend} repeat-${repetition}`);
            if (result.status !== 'pass') {
              console.error(JSON.stringify({
                caseId: example.id,
                scenarioId: scenario.id,
                pipeline,
                backend,
                failures: result.failures,
                oracleComparison: result.oracleComparison
              }));
            }
          }
        }
      }
    }
  }
  return quadrants;
}

/** Executes the repeatable closed subwave without promoting global batch status. */
async function main() {
  const options = parseArguments(process.argv);
  const binaryRoot = requirePath(options, 'binary-root');
  const generatedRoot = requirePath(options, 'generated-root');
  const assetRoot = requirePath(options, 'asset-root');
  const oracleRoot = requirePath(options, 'oracle-root');
  const outputDirectory = requirePath(options, 'output-dir');
  const manifestPath = path.resolve(
    options.manifest
      ?? path.join(repositoryRoot, 'GVMRuntime_ThreeSamples', 'Manifest',
        'three-r185-manifest.json'));
  const repetitions = Number(options.repeat ?? 3);
  const timeoutMs = Number(options['timeout-ms'] ?? 60_000);
  const selectedCaseIds = options['case-ids']
    ? new Set(options['case-ids'].split(',').filter(Boolean))
    : new Set(caseDefinitions.map((definition) => definition.caseId));
  const definitions = caseDefinitions.filter(
    (definition) => selectedCaseIds.has(definition.caseId));
  if (definitions.length !== selectedCaseIds.size || definitions.length === 0) {
    throw new Error('--case-ids must select one or more known closed-subwave cases.');
  }
  if (!Number.isInteger(repetitions) || repetitions < 3
      || !Number.isInteger(timeoutMs) || timeoutMs < 1) {
    throw new Error('Closed subwave requires --repeat >= 3 and a positive --timeout-ms.');
  }
  await fs.mkdir(outputDirectory, { recursive: true });
  const [examples, gpuBoundaryLint] = await Promise.all([
    loadExamples(manifestPath, definitions),
    lintSampleGpuBoundary(repositoryRoot)
  ]);
  const generatedArtifactLint = lintGeneratedArtifacts(generatedRoot, definitions);
  const context = {
    profile: {
      buildDir: outputDirectory
    },
    binaryRoot,
    sourceDir: repositoryRoot,
    runDir: outputDirectory,
    assetRoot,
    oracleRoot,
    timeoutMs,
    definitions
  };
  const quadrants = await runMatrix(context, examples, repetitions);
  const crossComparisons = await buildCrossComparisons(quadrants);
  const stabilityComparisons = await buildStabilityComparisons(quadrants, repetitions);
  const expectedRunCount = examples.reduce(
    (sum, example) => sum + example.scenarios.length, 0)
    * requiredPipelines.length * requiredBackends.length * repetitions;
  const status = quadrants.length === expectedRunCount
    && quadrants.every((entry) => entry.status === 'pass')
    && crossComparisons.every((entry) => entry.status === 'pass')
    && stabilityComparisons.every((entry) => entry.status === 'pass')
    && generatedArtifactLint.status === 'pass'
    && gpuBoundaryLint.status === 'pass'
    ? 'pass'
    : 'fail';
  const summary = {
    schemaVersion: 1,
    status,
    selectedCases: examples.map((example) => example.id),
    repeatCount: repetitions,
    singleSampleManifestOverrides: definitions
      .filter((definition) => definition.singleSampleManifestOverride)
      .map((definition) => ({
        caseId: definition.caseId,
        reason: 'Phase 1 renders the upstream antialias request as one ordinary single-sample scene pass without MSAA emulation.'
      })),
    runCount: quadrants.length,
    expectedRunCount,
    pipelines: [...requiredPipelines],
    backends: [...requiredBackends],
    quadrants,
    crossComparisons,
    stabilityComparisons,
    generatedArtifacts: [generatedArtifactLint],
    gpuBoundaryLint,
    thresholds: {
      normalizedDistancePixelRatio: 0.001,
      meanAbsoluteRgb: 2,
      p99AbsoluteRgb: 16,
      luminanceSsim: 0.995
    }
  };
  await writeJson(path.join(outputDirectory, 'summary.json'), summary);
  for (const example of examples) {
    await writeJson(path.join(outputDirectory, 'cases', example.id, 'summary.json'), summary);
  }
  console.log(`Batch-20 closed subwave ${status}: ${path.join(outputDirectory, 'summary.json')}`);
  if (status !== 'pass') process.exitCode = 1;
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.stack ?? error.message : String(error));
    process.exitCode = 1;
  });
}
