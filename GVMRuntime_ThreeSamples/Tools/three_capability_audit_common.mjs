import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

const MAX_CORE_EVIDENCE = 48;
const MAX_AUXILIARY_EVIDENCE = 24;

const FEATURE_DEFINITIONS = [
	{
		id: 'renderable_construction',
		detectors: [
			{ id: 'renderable_constructor', pattern: /\bnew\s+(?:THREE\.)?(?:Mesh|SkinnedMesh|InstancedMesh|BatchedMesh|Line|LineSegments|LineLoop|Line2|LineSegments2|Wireframe|Points|Sprite|Lensflare|Reflector|Refractor|Water|Sky|MarchingCubes|Text|[A-Za-z_$][\w$]*Helper)\s*\(/ }
		]
	},
	{
		id: 'instancing',
		detectors: [
			{ id: 'instanced_type', pattern: /\b(?:InstancedMesh|InstancedBufferGeometry|InstancedBufferAttribute|BatchedMesh)\b/ },
			{ id: 'instance_count', pattern: /\b(?:instanceCount|instancesPerElement)\s*[:=]/ },
			{ id: 'instance_update', pattern: /\.(?:setMatrixAt|setColorAt|getMatrixAt|getColorAt)\s*\(/ }
		]
	},
	{
		id: 'lod',
		detectors: [
			{ id: 'lod_type', pattern: /\bnew\s+(?:THREE\.)?LOD\s*\(/ },
			{ id: 'lod_level', pattern: /\.addLevel\s*\(/ }
		]
	},
	{
		id: 'hierarchy',
		detectors: [
			{ id: 'group_constructor', pattern: /\bnew\s+(?:THREE\.)?(?:Group|Bone)\s*\(/ },
			{ id: 'named_hierarchy_add', pattern: /\b(?:group|root|pivot|container|model|mesh|object|node|parent)[A-Za-z0-9_$]*\.add\s*\(/i },
			{ id: 'object_attach', pattern: /\.attach\s*\(/ }
		]
	},
	{
		id: 'dynamic_objects',
		detectors: [
			{ id: 'named_object_remove', pattern: /\b(?:scene|group|root|pivot|container|model|mesh|object|node|parent)[A-Za-z0-9_$]*\.(?:remove|clear)\s*\(/i },
			{ id: 'object_remove_from_parent', pattern: /\.removeFromParent\s*\(/ },
			{ id: 'object_visibility_update', pattern: /\.visible\s*=\s*(?!true\s*;|false\s*;)/ },
			{ id: 'object_reparent', pattern: /\.attach\s*\(/ }
		]
	},
	{
		id: 'scene_addition',
		detectors: [
			{ id: 'scene_add', pattern: /\bscene[A-Za-z0-9_$]*\.add\s*\(/i }
		]
	},
	{
		id: 'material_construction',
		detectors: [
			{ id: 'material_constructor', pattern: /\bnew\s+(?:THREE\.)?[A-Za-z_$][\w$]*(?:Node)?Material\s*\(/ },
			{ id: 'material_factory', pattern: /\b(?:createNodeMaterialFromType|ShaderMaterial|RawShaderMaterial)\s*\(/ }
		]
	},
	{
		id: 'material_array',
		detectors: [
			{ id: 'mesh_material_array', pattern: /\bnew\s+(?:THREE\.)?(?:Mesh|InstancedMesh|BatchedMesh)\s*\([^;]*\[[^\]]+\]/ },
			{ id: 'material_array_declaration', pattern: /\bmaterials?\s*=\s*\[/ }
		]
	},
	{
		id: 'geometry_groups',
		detectors: [
			{ id: 'geometry_add_group', pattern: /\.addGroup\s*\(/ },
			{ id: 'geometry_groups_access', pattern: /\.groups\b/ },
			{ id: 'geometry_clear_groups', pattern: /\.clearGroups\s*\(/ }
		]
	},
	{
		id: 'loader',
		detectors: [
			{ id: 'loader_symbol', pattern: /\b[A-Za-z_$][\w$]*Loader\b/ },
			{ id: 'loader_request', pattern: /\.(?:load|loadAsync|parse)\s*\(/ }
		]
	},
	{
		id: 'loader_multiple_renderables',
		detectors: [
			{ id: 'loaded_scene_traverse', pattern: /\.(?:scene|scenes|object)\b[^;]*\.traverse\s*\(/ },
			{ id: 'loaded_object_traverse', pattern: /\b(?:gltf|model|object|result|collada|asset)\b[^;]*\.traverse\s*\(/ },
			{ id: 'multi_object_loader', pattern: /\b(?:GLTFLoader|FBXLoader|ColladaLoader|OBJLoader|LDrawLoader|ThreeMFLoader|AMFLoader|PDBLoader|VRMLLoader|USDZLoader)\b/ }
		]
	},
	{
		id: 'postprocessing',
		detectors: [
			{ id: 'postprocess_symbol', pattern: /\b(?:EffectComposer|PostProcessing|RenderPass|ShaderPass|OutputPass|BloomPass|UnrealBloomPass|TAARenderPass|SSAOPass|SSRPass|BokehPass|AfterimagePass|GlitchPass|OutlinePass|MaskPass|ClearMaskPass)\b/ },
			{ id: 'postprocess_add_pass', pattern: /\.addPass\s*\(/ }
		]
	},
	{
		id: 'compute',
		detectors: [
			{ id: 'compute_dispatch', pattern: /\.(?:compute|computeAsync)\s*\(/ },
			{ id: 'compute_symbol', pattern: /\b(?:GPUComputationRenderer|ComputeNode|computeKernel|workgroupArray|workgroupBarrier|storageBarrier|storageObject|storageTexture)\b/ }
		]
	},
	{
		id: 'shadow',
		detectors: [
			{ id: 'shadow_enable', pattern: /\.(?:castShadow|receiveShadow|shadowMap\.enabled)\s*=\s*true\b/ },
			{ id: 'shadow_material', pattern: /\b(?:ShadowMaterial|MeshDepthMaterial|MeshDistanceMaterial)\b/ }
		]
	},
	{
		id: 'picking',
		detectors: [
			{ id: 'raycaster', pattern: /\bRaycaster\b|\.intersectObjects?\s*\(/ },
			{ id: 'gpu_picking', pattern: /\breadRenderTargetPixels(?:Async)?\s*\(|\b(?:picking|pickId|objectId)\b/i }
		]
	},
	{
		id: 'reflection',
		detectors: [
			{ id: 'reflection_capture', pattern: /\b(?:CubeCamera|Reflector|Refractor|PMREMGenerator)\b/ },
			{ id: 'environment_map', pattern: /\b(?:envMap|environment)\s*[:=]/ }
		]
	},
	{
		id: 'animation',
		detectors: [
			{ id: 'animation_loop', pattern: /\b(?:setAnimationLoop|requestAnimationFrame|AnimationMixer|AnimationClip|AnimationAction)\b/ },
			{ id: 'animation_callback', pattern: /\bfunction\s+(?:animate|render|update)\s*\(/ }
		]
	},
	{
		id: 'interaction',
		detectors: [
			{ id: 'input_listener', pattern: /addEventListener\s*\(\s*['"](?:pointer|mouse|touch|key|wheel)/ },
			{ id: 'controls_symbol', pattern: /\b(?:OrbitControls|TrackballControls|MapControls|TransformControls|DragControls|FirstPersonControls|FlyControls|ArcballControls)\b/ },
			{ id: 'gui_symbol', pattern: /\bGUI\b|\.add\s*\([^;]*onChange/ }
		]
	},
	{
		id: 'exporter',
		detectors: [
			{ id: 'exporter_symbol', pattern: /\b[A-Za-z_$][\w$]*Exporter\b/ }
		]
	},
	{
		id: 'native_line_or_point',
		detectors: [
			{ id: 'line_point_constructor', pattern: /\bnew\s+(?:THREE\.)?(?:Line|LineSegments|LineLoop|Points)\s*\(/ },
			{ id: 'line_point_material', pattern: /\b(?:LineBasicMaterial|LineDashedMaterial|PointsMaterial)\b/ }
		]
	},
	{
		id: 'transparency',
		detectors: [
			{ id: 'transparent_material', pattern: /\btransparent\s*:\s*true\b|\.transparent\s*=\s*true\b/ },
			{ id: 'material_opacity', pattern: /\bopacity\s*[:=]\s*(?:0?\.[0-9]+|0)\b/ }
		]
	}
];

const MISSING_CAPABILITY_RULES = [
	{
		capability: 'multisample_resolve',
		coreConfidence: 'high',
		detector: 'explicit_render_target_samples',
		pattern: /\.(?:samples|resolveDepthBuffer|resolveStencilBuffer)\s*=|\b(?:WebGLRenderTarget|WebGPURenderTarget|RenderTarget)\s*\([^;\n]*\bsamples\s*:/,
		rationale: 'The example explicitly configures multisample render-target or resolve behavior.'
	},
	{
		capability: 'complete_stencil_operations_and_reference',
		coreConfidence: 'high',
		detector: 'explicit_stencil_state',
		pattern: /\b(?:stencilWrite|stencilFunc|stencilRef|stencilFail|stencilZFail|stencilZPass|stencilWriteMask|stencilFuncMask|Stencil[A-Z][A-Za-z]+)\b/,
		rationale: 'The example explicitly names stencil state or an operation constant.'
	},
	{
		capability: 'dsl_texture_cube_array',
		coreConfidence: 'high',
		detector: 'cube_array_type_or_sampler',
		pattern: /\b(?:CubeTextureArray|samplerCubeArray|textureCubeArray)\b/,
		rationale: 'The example explicitly uses a cube-array texture type or shader sampler.'
	},
	{
		capability: 'dsl_texture_cube',
		coreConfidence: 'medium',
		detector: 'cube_texture_type_or_sampler',
		pattern: /\b(?:CubeTexture|CubeTextureLoader|CubeCamera|WebGLCubeRenderTarget|CubeRenderTarget|samplerCube|textureCube)\b/,
		rationale: 'The example names cube-texture sampling or capture; atlas or array emulation still requires manual semantic review.'
	},
	{
		capability: 'automatic_mipmap_generation',
		coreConfidence: 'medium',
		detector: 'explicit_mipmap_generation',
		pattern: /\b(?:generateMipmaps|generateMipmap)\b|\bminFilter\s*[:=][^;\n]*Mipmap/,
		rationale: 'The example explicitly requests generated mipmaps or a mipmap minification mode.'
	},
	{
		capability: 'front_facing_builtin',
		coreConfidence: 'high',
		detector: 'front_facing_shader_builtin',
		pattern: /\b(?:gl_FrontFacing|frontFacing|FrontFacingNode)\b/,
		rationale: 'The example shader explicitly consumes the fragment front-facing builtin.'
	},
	{
		capability: 'point_size_builtin',
		coreConfidence: 'high',
		detector: 'point_size_shader_builtin',
		pattern: /\b(?:gl_PointSize|pointSizeNode|PointSizeNode)\b/,
		rationale: 'The example shader explicitly writes or constructs point-size builtin state.'
	},
	{
		capability: 'sample_mask_builtin',
		coreConfidence: 'high',
		detector: 'sample_mask_shader_builtin',
		pattern: /\b(?:gl_SampleMask|sampleMask|SampleMask|alphaToCoverage)\b/,
		rationale: 'The example explicitly consumes sample-mask state or requests alpha-to-coverage multisample behavior.'
	},
	{
		capability: 'blend_constant',
		coreConfidence: 'high',
		detector: 'constant_blend_factor',
		pattern: /\b(?:blendColor|blendAlpha|ConstantColorFactor|OneMinusConstantColorFactor|ConstantAlphaFactor|OneMinusConstantAlphaFactor)\b/,
		rationale: 'The example explicitly requests a constant blend factor or value.'
	},
	{
		capability: 'occlusion_query',
		coreConfidence: 'high',
		detector: 'occlusion_query_api',
		pattern: /\b(?:occlusionTest|OcclusionQuery|isObjectVisible)\b/,
		rationale: 'The example explicitly requests occlusion-query behavior.'
	},
	{
		capability: 'new_shader_stage',
		coreConfidence: 'high',
		detector: 'unsupported_shader_stage',
		pattern: /\b(?:geometryShader|tessellationControlShader|tessellationEvaluationShader|meshShader|taskShader)\b/,
		rationale: 'The example explicitly declares a shader stage outside vertex, fragment, and compute.'
	},
	{
		capability: 'render_set_sort',
		coreConfidence: 'medium',
		detector: 'explicit_renderer_sort_control',
		pattern: /\b(?:sortObjects|renderOrder)\s*=/,
		rationale: 'The example explicitly controls render ordering, but static entity order may remain equivalent.'
	}
];

/** Returns a stable SHA-256 digest for a UTF-8 string. */
export function computeSha256( text ) {

	return crypto.createHash( 'sha256' ).update( text ).digest( 'hex' );

}

/** Converts an absolute path under the upstream root to a portable source path. */
function toSourcePath( upstreamRoot, absolutePath ) {

	return path.relative( upstreamRoot, absolutePath ).split( path.sep ).join( '/' );

}

/** Normalizes a source line into a bounded review excerpt without changing line identity. */
function createExcerpt( line ) {

	const normalized = line.trim().replace( /\s+/g, ' ' );
	return normalized.length <= 240 ? normalized : `${ normalized.slice( 0, 237 ) }...`;

}

/** Masks JavaScript and HTML comments while preserving every source offset and line break. */
function maskSourceComments( sourceText ) {

	const characters = [ ...sourceText ];
	let state = 'code';
	for ( let index = 0; index < characters.length; index ++ ) {

		const character = characters[ index ];
		const nextCharacter = characters[ index + 1 ];
		if ( state === 'line_comment' ) {

			if ( character === '\n' || character === '\r' ) state = 'code';
			else characters[ index ] = ' ';
			continue;

		}
		if ( state === 'block_comment' ) {

			if ( character === '*' && nextCharacter === '/' ) {

				characters[ index ] = ' ';
				characters[ index + 1 ] = ' ';
				index ++;
				state = 'code';

			} else if ( character !== '\n' && character !== '\r' ) characters[ index ] = ' ';
			continue;

		}
		if ( state === 'html_comment' ) {

			if ( sourceText.startsWith( '-->', index ) ) {

				characters[ index ] = ' ';
				characters[ index + 1 ] = ' ';
				characters[ index + 2 ] = ' ';
				index += 2;
				state = 'code';

			} else if ( character !== '\n' && character !== '\r' ) characters[ index ] = ' ';
			continue;

		}
		if ( state === 'single_quote' || state === 'double_quote' || state === 'template_quote' ) {

			if ( character === '\\' ) {

				index ++;
				continue;

			}
			if ( state === 'single_quote' && character === '\'' || state === 'double_quote' && character === '"' || state === 'template_quote' && character === '`' ) state = 'code';
			continue;

		}
		if ( sourceText.startsWith( '<!--', index ) ) {

			characters[ index ] = ' ';
			characters[ index + 1 ] = ' ';
			characters[ index + 2 ] = ' ';
			characters[ index + 3 ] = ' ';
			index += 3;
			state = 'html_comment';
			continue;

		}
		if ( character === '/' && nextCharacter === '/' ) {

			characters[ index ] = ' ';
			characters[ index + 1 ] = ' ';
			index ++;
			state = 'line_comment';
			continue;

		}
		if ( character === '/' && nextCharacter === '*' ) {

			characters[ index ] = ' ';
			characters[ index + 1 ] = ' ';
			index ++;
			state = 'block_comment';
			continue;

		}
		if ( character === '\'' ) state = 'single_quote';
		else if ( character === '"' ) state = 'double_quote';
		else if ( character === '`' ) state = 'template_quote';

	}
	return characters.join( '' );

}

/** Builds one exact source evidence record for a detector match. */
function createEvidence( sourcePath, lineNumber, columnNumber, line, detector, role ) {

	return {
		sourcePath,
		line: lineNumber,
		column: columnNumber,
		detector,
		role,
		excerpt: createExcerpt( line )
	};

}

/** Sorts evidence deterministically by source location and detector identifier. */
function compareEvidence( left, right ) {

	return left.sourcePath.localeCompare( right.sourcePath ) ||
		left.line - right.line ||
		left.column - right.column ||
		left.detector.localeCompare( right.detector );

}

/** Deduplicates and bounds evidence while reporting how many matches were omitted. */
function boundEvidence( evidence, maximum ) {

	const uniqueEvidence = [];
	const seen = new Set();
	for ( const item of evidence.sort( compareEvidence ) ) {

		const key = `${ item.sourcePath }:${ item.line }:${ item.column }:${ item.detector }`;
		if ( seen.has( key ) ) continue;
		seen.add( key );
		uniqueEvidence.push( item );

	}

	return {
		evidence: uniqueEvidence.slice( 0, maximum ),
		omittedEvidenceCount: Math.max( 0, uniqueEvidence.length - maximum )
	};

}

/** Deduplicates semantic construction sites that matched more than one detector on the same source line. */
function uniqueSourceSites( evidence ) {

	const sites = [];
	const seen = new Set();
	for ( const item of evidence.sort( compareEvidence ) ) {

		const key = `${ item.sourcePath }:${ item.line }`;
		if ( seen.has( key ) ) continue;
		seen.add( key );
		sites.push( item );

	}
	return sites;

}

/** Scans one source document for all declared feature detectors. */
export function scanSourceFeatures( sourcePath, sourceText ) {

	const results = new Map();
	for ( const definition of FEATURE_DEFINITIONS ) results.set( definition.id, [] );
	const lines = sourceText.split( /\r?\n/ );
	const searchableLines = maskSourceComments( sourceText ).split( /\r?\n/ );
	for ( let lineIndex = 0; lineIndex < lines.length; lineIndex ++ ) {

		const line = lines[ lineIndex ];
		const searchableLine = searchableLines[ lineIndex ];
		for ( const definition of FEATURE_DEFINITIONS ) {

			for ( const detector of definition.detectors ) {

				const match = searchableLine.match( detector.pattern );
				if ( match === null ) continue;
				results.get( definition.id ).push( createEvidence(
					sourcePath,
					lineIndex + 1,
					match.index + 1,
					line,
					detector.id,
					'example_source'
				) );

			}

		}

	}
	return results;

}

/** Extracts static JavaScript module specifiers and their exact source locations. */
export function extractModuleSpecifiers( sourcePath, sourceText ) {

	const specifiers = [];
	const lines = sourceText.split( /\r?\n/ );
	const searchableText = maskSourceComments( sourceText );
	const patterns = [
		{ id: 'static_import_or_export', pattern: /^[\t ]*(?:import|export)\s+(?!\()[^;]*?(?:\bfrom\s*)?['"]([^'"]+)['"][^;]*;/gm },
		{ id: 'dynamic_import', pattern: /\bimport\s*\(\s*['"]([^'"]+)['"]\s*\)/g },
		{ id: 'import_meta_url_module', pattern: /\bnew\s+URL\s*\(\s*['"]([^'"]+\.(?:js|mjs))['"]\s*,\s*import\.meta\.url\s*\)/g }
	];
	for ( const detector of patterns ) {

		detector.pattern.lastIndex = 0;
		let match = detector.pattern.exec( searchableText );
		while ( match !== null ) {

			const specifierOffset = match.index + match[ 0 ].lastIndexOf( match[ 1 ] );
			const sourcePrefix = sourceText.slice( 0, specifierOffset );
			const lineNumber = sourcePrefix.split( /\r?\n/ ).length;
			const lineStart = Math.max( sourcePrefix.lastIndexOf( '\n' ), sourcePrefix.lastIndexOf( '\r' ) ) + 1;
			specifiers.push( {
				specifier: match[ 1 ],
				evidence: createEvidence( sourcePath, lineNumber, specifierOffset - lineStart + 1, lines[ lineNumber - 1 ], detector.id, 'example_source' )
			} );
			match = detector.pattern.exec( searchableText );

		}

	}
	return specifiers.sort( function compareSpecifiers( left, right ) {

		return compareEvidence( left.evidence, right.evidence ) || left.specifier.localeCompare( right.specifier );

	} );

}

/** Resolves a static specifier when it names a module inside the pinned upstream tree. */
export function resolveLocalModuleSpecifier( upstreamRoot, sourcePath, specifier ) {

	const cleanSpecifier = specifier.split( /[?#]/, 1 )[ 0 ];
	let candidate = null;
	if ( cleanSpecifier.startsWith( 'three/addons/' ) ) {

		candidate = path.join( upstreamRoot, 'examples/jsm', cleanSpecifier.slice( 'three/addons/'.length ) );

	} else if ( cleanSpecifier.startsWith( './' ) || cleanSpecifier.startsWith( '../' ) ) {

		candidate = path.resolve( upstreamRoot, path.dirname( sourcePath ), cleanSpecifier );

	} else {

		return null;

	}

	if ( path.extname( candidate ) === '' ) candidate = `${ candidate }.js`;
	const relativeCandidate = toSourcePath( upstreamRoot, candidate );
	if ( relativeCandidate.startsWith( '../' ) || path.isAbsolute( relativeCandidate ) ) return null;
	if ( ! relativeCandidate.startsWith( 'examples/' ) ) return null;
	if ( ! /\.(?:js|mjs)$/.test( relativeCandidate ) ) return null;
	return relativeCandidate;

}

/** Finds Scene constructor sites in the example source without inferring runtime ownership. */
export function extractSceneRoots( sourcePath, sourceText ) {

	const roots = [];
	const lines = sourceText.split( /\r?\n/ );
	const searchableLines = maskSourceComments( sourceText ).split( /\r?\n/ );
	const constructorPattern = /(?:\b(?:const|let|var)\s+)?([A-Za-z_$][\w$]*(?:\s*\[[^\]]+\])?)?\s*=\s*new\s+(?:THREE\.)?Scene\s*\(|\bnew\s+(?:THREE\.)?Scene\s*\(/g;
	for ( let lineIndex = 0; lineIndex < lines.length; lineIndex ++ ) {

		const line = lines[ lineIndex ];
		const searchableLine = searchableLines[ lineIndex ];
		constructorPattern.lastIndex = 0;
		let match = constructorPattern.exec( searchableLine );
		while ( match !== null ) {

			const rootName = match[ 1 ]?.replace( /\s+/g, '' ) ?? `<anonymous@${ lineIndex + 1 }>`;
			roots.push( {
				name: rootName,
				evidence: createEvidence( sourcePath, lineIndex + 1, match.index + 1, line, 'scene_constructor', 'example_source' ),
				manualDecision: 'pending'
			} );
			match = constructorPattern.exec( searchableLine );

		}

	}
	return roots;

}

/** Scans the example source for exact frozen-capability signals requiring manual adjudication. */
export function scanMissingCapabilityCandidates( sourcePath, sourceText, frozenCapabilities ) {

	const candidates = new Map();
	const lines = sourceText.split( /\r?\n/ );
	const searchableLines = maskSourceComments( sourceText ).split( /\r?\n/ );
	for ( let lineIndex = 0; lineIndex < lines.length; lineIndex ++ ) {

		const line = lines[ lineIndex ];
		const searchableLine = searchableLines[ lineIndex ];
		for ( const rule of MISSING_CAPABILITY_RULES ) {

			if ( ! frozenCapabilities.has( rule.capability ) ) throw new Error( `Detector references unfrozen capability '${ rule.capability }'.` );
			const match = searchableLine.match( rule.pattern );
			if ( match === null ) continue;
			const existing = candidates.get( rule.capability ) ?? {
				capability: rule.capability,
				detectorStrength: 'exact_signal',
				coreConfidence: rule.coreConfidence,
				manualDecision: 'pending',
				statusEffect: 'none',
				rationale: rule.rationale,
				evidence: []
			};
			existing.evidence.push( createEvidence(
				sourcePath,
				lineIndex + 1,
				match.index + 1,
				line,
					rule.detector,
				'example_source'
			) );
			if ( rule.coreConfidence === 'high' ) existing.coreConfidence = 'high';
			candidates.set( rule.capability, existing );

		}

	}
	return [ ...candidates.values() ].sort( function compareCandidates( left, right ) {

		return left.capability.localeCompare( right.capability );

	} );

}

/** Reads and caches one UTF-8 source file under the upstream root. */
function readSource( upstreamRoot, sourcePath, sourceCache ) {

	if ( sourceCache.has( sourcePath ) ) return sourceCache.get( sourcePath );
	const absolutePath = path.resolve( upstreamRoot, sourcePath );
	const relativePath = toSourcePath( upstreamRoot, absolutePath );
	if ( relativePath.startsWith( '../' ) || path.isAbsolute( relativePath ) ) throw new Error( `Source escapes upstream root: ${ sourcePath }` );
	if ( ! fs.existsSync( absolutePath ) ) return null;
	const sourceText = fs.readFileSync( absolutePath, 'utf8' );
	sourceCache.set( sourcePath, sourceText );
	return sourceText;

}

/** Returns direct local module edges for one source document. */
function readDirectDependencies( upstreamRoot, sourcePath, sourceCache, dependencyCache ) {

	if ( dependencyCache.has( sourcePath ) ) return dependencyCache.get( sourcePath );
	const sourceText = readSource( upstreamRoot, sourcePath, sourceCache );
	if ( sourceText === null ) throw new Error( `Missing source document: ${ sourcePath }` );
	const dependencies = [];
	for ( const importRecord of extractModuleSpecifiers( sourcePath, sourceText ) ) {

		const resolvedSourcePath = resolveLocalModuleSpecifier( upstreamRoot, sourcePath, importRecord.specifier );
		if ( resolvedSourcePath === null ) continue;
		dependencies.push( {
			sourcePath: resolvedSourcePath,
			specifier: importRecord.specifier,
			evidence: importRecord.evidence,
			exists: readSource( upstreamRoot, resolvedSourcePath, sourceCache ) !== null
		} );

	}
	dependencies.sort( function compareDependencies( left, right ) {

		return left.sourcePath.localeCompare( right.sourcePath ) || compareEvidence( left.evidence, right.evidence );

	} );
	dependencyCache.set( sourcePath, dependencies );
	return dependencies;

}

/** Traverses all reachable local module dependencies and retains shortest import paths. */
function collectDependencyGraph( upstreamRoot, entrySourcePath, sourceCache, dependencyCache ) {

	const queue = [ { sourcePath: entrySourcePath, depth: 0 } ];
	const visitedDepth = new Map( [ [ entrySourcePath, 0 ] ] );
	const records = new Map();
	const missing = [];
	for ( let queueIndex = 0; queueIndex < queue.length; queueIndex ++ ) {

		const current = queue[ queueIndex ];
		for ( const dependency of readDirectDependencies( upstreamRoot, current.sourcePath, sourceCache, dependencyCache ) ) {

			const importEvidence = {
				...dependency.evidence,
				role: current.sourcePath === entrySourcePath ? 'example_source' : 'local_dependency'
			};

			if ( ! dependency.exists ) {

				missing.push( {
					sourcePath: dependency.sourcePath,
					importedBy: current.sourcePath,
					specifier: dependency.specifier,
					evidence: importEvidence
				} );
				continue;

			}
			const nextDepth = current.depth + 1;
			const existingDepth = visitedDepth.get( dependency.sourcePath );
			if ( existingDepth === undefined || nextDepth < existingDepth ) {

				visitedDepth.set( dependency.sourcePath, nextDepth );
				records.set( dependency.sourcePath, {
					sourcePath: dependency.sourcePath,
					depth: nextDepth,
					importedBy: current.sourcePath,
					specifier: dependency.specifier,
					evidence: importEvidence
				} );
				queue.push( { sourcePath: dependency.sourcePath, depth: nextDepth } );

			}

		}

	}
	return {
		dependencies: [ ...records.values() ].sort( function compareDependencyRecords( left, right ) {

			return left.depth - right.depth || left.sourcePath.localeCompare( right.sourcePath );

		} ),
		missingDependencies: missing.sort( function compareMissingDependencies( left, right ) {

			return left.sourcePath.localeCompare( right.sourcePath ) || left.importedBy.localeCompare( right.importedBy );

		} )
	};

}

/** Recursively collects every source evidence object embedded in an audit value. */
function collectEmbeddedEvidence( value, evidenceRecords ) {

	if ( value === null || typeof value !== 'object' ) return;
	if ( typeof value.sourcePath === 'string' && Number.isInteger( value.line ) && Number.isInteger( value.column ) && typeof value.excerpt === 'string' ) evidenceRecords.push( value );
	if ( Array.isArray( value ) ) {

		for ( const item of value ) collectEmbeddedEvidence( item, evidenceRecords );
		return;

	}
	for ( const nestedValue of Object.values( value ) ) collectEmbeddedEvidence( nestedValue, evidenceRecords );

}

/** Verifies that one evidence record resolves to the exact committed source line and column. */
function validateEvidenceLocation( upstreamRoot, evidence, sourceCache ) {

	const sourceText = readSource( upstreamRoot, evidence.sourcePath, sourceCache );
	if ( sourceText === null ) return `evidence source does not exist: ${ evidence.sourcePath }`;
	const lines = sourceText.split( /\r?\n/ );
	if ( evidence.line < 1 || evidence.line > lines.length ) return `evidence line is out of range: ${ evidence.sourcePath }:${ evidence.line }`;
	const line = lines[ evidence.line - 1 ];
	if ( evidence.column < 1 || evidence.column > line.length + 1 ) return `evidence column is out of range: ${ evidence.sourcePath }:${ evidence.line }:${ evidence.column }`;
	if ( evidence.excerpt !== createExcerpt( line ) ) return `evidence excerpt is stale: ${ evidence.sourcePath }:${ evidence.line }`;
	if ( evidence.role !== 'example_source' && evidence.role !== 'local_dependency' ) return `evidence role is invalid: ${ evidence.sourcePath }:${ evidence.line }`;
	return null;

}

/** Returns cached feature matches for one source document. */
function readFeatureMatches( upstreamRoot, sourcePath, sourceCache, featureCache ) {

	if ( featureCache.has( sourcePath ) ) return featureCache.get( sourcePath );
	const sourceText = readSource( upstreamRoot, sourcePath, sourceCache );
	if ( sourceText === null ) throw new Error( `Missing source document: ${ sourcePath }` );
	const matches = scanSourceFeatures( sourcePath, sourceText );
	featureCache.set( sourcePath, matches );
	return matches;

}

/** Combines core entry-source and auxiliary dependency feature evidence without conflating their policy impact. */
function collectFeatures( upstreamRoot, entrySourcePath, dependencies, sourceCache, featureCache ) {

	const coreMatches = readFeatureMatches( upstreamRoot, entrySourcePath, sourceCache, featureCache );
	const featureRecords = {};
	for ( const definition of FEATURE_DEFINITIONS ) {

		const coreBound = boundEvidence( [ ...coreMatches.get( definition.id ) ], MAX_CORE_EVIDENCE );
		const auxiliaryEvidence = [];
		for ( const dependency of dependencies ) {

			for ( const evidence of readFeatureMatches( upstreamRoot, dependency.sourcePath, sourceCache, featureCache ).get( definition.id ) ) {

				auxiliaryEvidence.push( { ...evidence, role: 'local_dependency' } );

			}

		}
		const auxiliaryBound = boundEvidence( auxiliaryEvidence, MAX_AUXILIARY_EVIDENCE );
		featureRecords[ definition.id ] = {
			coreDetected: coreBound.evidence.length > 0,
			auxiliaryDetected: auxiliaryBound.evidence.length > 0,
			coreEvidence: coreBound.evidence,
			auxiliaryEvidence: auxiliaryBound.evidence,
			omittedCoreEvidenceCount: coreBound.omittedEvidenceCount,
			omittedAuxiliaryEvidenceCount: auxiliaryBound.omittedEvidenceCount
		};

	}
	return featureRecords;

}

/** Returns cached frozen-capability matches for one source document. */
function readCapabilityMatches( upstreamRoot, sourcePath, frozenCapabilities, sourceCache, capabilityCache ) {

	if ( capabilityCache.has( sourcePath ) ) return capabilityCache.get( sourcePath );
	const sourceText = readSource( upstreamRoot, sourcePath, sourceCache );
	if ( sourceText === null ) throw new Error( `Missing source document: ${ sourcePath }` );
	const matches = scanMissingCapabilityCandidates( sourcePath, sourceText, frozenCapabilities );
	capabilityCache.set( sourcePath, matches );
	return matches;

}

/** Collects dependency-only capability signals without promoting them to core candidates. */
function collectAuxiliaryCapabilitySignals( upstreamRoot, dependencies, frozenCapabilities, sourceCache, capabilityCache ) {

	const signals = new Map();
	for ( const dependency of dependencies ) {

		for ( const candidate of readCapabilityMatches( upstreamRoot, dependency.sourcePath, frozenCapabilities, sourceCache, capabilityCache ) ) {

			const existing = signals.get( candidate.capability ) ?? {
				capability: candidate.capability,
				detectorStrength: 'exact_dependency_signal',
				coreConfidence: 'auxiliary_only',
				manualDecision: 'pending',
				statusEffect: 'none',
				rationale: 'The signal occurs only in a reachable local dependency; usage by this example must be proven manually.',
				evidence: [],
				omittedEvidenceCount: 0
			};
			for ( const evidence of candidate.evidence ) existing.evidence.push( { ...evidence, role: 'local_dependency' } );
			signals.set( candidate.capability, existing );

		}

	}
	for ( const signal of signals.values() ) {

		const bounded = boundEvidence( signal.evidence, MAX_AUXILIARY_EVIDENCE );
		signal.evidence = bounded.evidence;
		signal.omittedEvidenceCount = bounded.omittedEvidenceCount;

	}
	return [ ...signals.values() ].sort( function compareSignals( left, right ) {

		return left.capability.localeCompare( right.capability );

	} );

}

/** Returns the candidate state and exact core evidence for one RenderSet trigger. */
function createRenderSetTrigger( candidate, evidence, basis ) {

	return {
		candidate,
		manualDecision: 'pending',
		basis,
		evidence
	};

}

/** Derives conservative RenderSet trigger candidates from core evidence only. */
function buildRenderSetAudit( sceneRoots, features ) {

	const renderableEvidence = uniqueSourceSites( features.renderable_construction.coreEvidence );
	const materialEvidence = uniqueSourceSites( features.material_construction.coreEvidence );
	const instancingEvidence = features.instancing.coreEvidence;
	const hierarchyEvidence = features.hierarchy.coreEvidence;
	const lodEvidence = features.lod.coreEvidence;
	const dynamicEvidence = features.dynamic_objects.coreEvidence;
	const sceneAdditionReviewEvidence = features.scene_addition.coreEvidence;
	const groupEvidence = features.geometry_groups.coreEvidence;
	const loaderEvidence = features.loader_multiple_renderables.coreEvidence;
	const multipleMaterialEvidence = uniqueSourceSites( [ ...materialEvidence, ...features.material_array.coreEvidence ] );
	return {
		manualPolicyDecision: 'pending',
		manifestMutationAllowed: false,
		sceneRoots,
		staticRenderableConstructionSiteCount: renderableEvidence.length + features.renderable_construction.omittedCoreEvidenceCount,
		staticMaterialConstructionSiteCount: materialEvidence.length + features.material_construction.omittedCoreEvidenceCount,
		triggers: {
			instancing: createRenderSetTrigger( instancingEvidence.length > 0, instancingEvidence, 'Exact instancing API evidence in the example source.' ),
			multiple_renderables: createRenderSetTrigger( renderableEvidence.length >= 2, renderableEvidence, 'At least two static renderable-construction sites; runtime multiplicity still needs review.' ),
			hierarchy: createRenderSetTrigger( hierarchyEvidence.length > 0, hierarchyEvidence, 'Group, nested add, or attach evidence in the example source.' ),
			lod: createRenderSetTrigger( lodEvidence.length > 0, lodEvidence, 'LOD constructor or addLevel evidence in the example source.' ),
			dynamic_objects: {
				...createRenderSetTrigger( dynamicEvidence.length > 0, dynamicEvidence, 'Remove, clear, reparent, or non-constant visibility evidence in the example source.' ),
				lifecycleReviewEvidence: sceneAdditionReviewEvidence,
				lifecycleReviewNote: 'Scene add calls are recorded for lifecycle review but do not imply runtime mutation without control-flow adjudication.'
			},
			multiple_materials: createRenderSetTrigger( materialEvidence.length >= 2 || features.material_array.coreEvidence.length > 0, multipleMaterialEvidence, 'Multiple unique material construction sites or an explicit material array; semantic deduplication needs review.' ),
			geometry_groups: createRenderSetTrigger( groupEvidence.length > 0, groupEvidence, 'Geometry group API evidence in the example source.' ),
			loader_multiple_renderables: createRenderSetTrigger( loaderEvidence.length > 0, loaderEvidence, 'A multi-object loader or traversal signal; the canonical loaded asset still needs inspection.' )
		},
		notes: [
			'Candidates are evidence only and never assign renderSetPolicy.',
			'Auxiliary dependency signals are excluded from automatic trigger candidates.',
			'Static constructor-site counts are not runtime object counts.'
		]
	};

}

/** Audits one non-excluded manifest example against its complete local module closure. */
function auditExample( upstreamRoot, example, frozenCapabilities, caches ) {

	const entrySourcePath = example.upstreamPath;
	const sourceText = readSource( upstreamRoot, entrySourcePath, caches.source );
	if ( sourceText === null ) throw new Error( `${ example.id }: missing upstream source ${ entrySourcePath }` );
	const dependencyGraph = collectDependencyGraph( upstreamRoot, entrySourcePath, caches.source, caches.dependencies );
	const features = collectFeatures( upstreamRoot, entrySourcePath, dependencyGraph.dependencies, caches.source, caches.features );
	const sceneRoots = extractSceneRoots( entrySourcePath, sourceText );
	return {
		id: example.id,
		category: example.category,
		upstreamPath: entrySourcePath,
		manifestStatusAtAudit: example.status,
		localModules: dependencyGraph.dependencies,
		missingLocalModules: dependencyGraph.missingDependencies,
		features,
		renderSetAudit: buildRenderSetAudit( sceneRoots, features ),
		missingCapabilityCandidates: scanMissingCapabilityCandidates( entrySourcePath, sourceText, frozenCapabilities ),
		auxiliaryMissingCapabilitySignals: collectAuxiliaryCapabilitySignals( upstreamRoot, dependencyGraph.dependencies, frozenCapabilities, caches.source, caches.capabilities ),
		classification: {
			decision: 'manual_review_pending',
			proposedStatus: null,
			manifestMutationAllowed: false
		}
	};

}

/** Counts evidence outcomes for the generated audit document. */
function countAuditResults( examples ) {

	let examplesWithMissingCapabilityCandidates = 0;
	let examplesWithAuxiliaryCapabilitySignals = 0;
	let examplesWithRenderSetTriggerCandidates = 0;
	let missingLocalModuleCount = 0;
	const capabilityCandidateCounts = {};
	const renderSetTriggerCounts = {};
	for ( const example of examples ) {

		if ( example.missingCapabilityCandidates.length > 0 ) examplesWithMissingCapabilityCandidates ++;
		if ( example.auxiliaryMissingCapabilitySignals.length > 0 ) examplesWithAuxiliaryCapabilitySignals ++;
		if ( Object.values( example.renderSetAudit.triggers ).some( function hasCandidate( trigger ) { return trigger.candidate; } ) ) examplesWithRenderSetTriggerCandidates ++;
		missingLocalModuleCount += example.missingLocalModules.length;
		for ( const candidate of example.missingCapabilityCandidates ) {

			capabilityCandidateCounts[ candidate.capability ] = ( capabilityCandidateCounts[ candidate.capability ] ?? 0 ) + 1;

		}
		for ( const [ triggerName, trigger ] of Object.entries( example.renderSetAudit.triggers ) ) {

			if ( trigger.candidate ) renderSetTriggerCounts[ triggerName ] = ( renderSetTriggerCounts[ triggerName ] ?? 0 ) + 1;

		}

	}
	return {
		auditedExamples: examples.length,
		examplesWithMissingCapabilityCandidates,
		examplesWithAuxiliaryCapabilitySignals,
		examplesWithRenderSetTriggerCandidates,
		missingLocalModuleCount,
		capabilityCandidateCounts: Object.fromEntries( Object.entries( capabilityCandidateCounts ).sort() ),
		renderSetTriggerCounts: Object.fromEntries( Object.entries( renderSetTriggerCounts ).sort() )
	};

}

/** Creates the stable inventory-only manifest used as Phase 0 audit input. */
export function createCapabilityAuditInputManifest( manifest ) {

	return {
		schemaVersion: manifest.schemaVersion ?? 1,
		upstream: structuredClone( manifest.upstream ),
		examples: manifest.examples.map( function normalizeAuditExample( example ) {

			const excluded = example.status === 'excluded_upstream' || example.exclusion !== null && example.exclusion !== undefined;
			return {
				id: example.id,
				category: example.category,
				upstreamPath: example.upstreamPath,
				status: excluded ? 'excluded_upstream' : 'audit_pending'
			};

		} )
	};

}

/** Generates the complete deterministic r185 audit document without changing manifest classifications. */
export function buildCapabilityAudit( upstreamRoot, manifest, capabilityFreeze, manifestText, capabilityFreezeText ) {

	const frozenCapabilities = new Set( capabilityFreeze.unsupportedPublicCapabilities );
	const candidates = manifest.examples.filter( function isAuditCandidate( example ) { return example.status !== 'excluded_upstream'; } );
	const caches = {
		source: new Map(),
		dependencies: new Map(),
		features: new Map(),
		capabilities: new Map()
	};
	const auditedExamples = candidates.map( function auditCandidate( example ) {

		return auditExample( upstreamRoot, example, frozenCapabilities, caches );

	} );
	return {
		schemaVersion: 1,
		upstream: {
			release: manifest.upstream.release,
			commit: manifest.upstream.commit
		},
		inputs: {
			manifestSha256: computeSha256( manifestText ),
			capabilityFreezeSha256: computeSha256( capabilityFreezeText )
		},
		policy: {
			classificationMode: 'evidence_only_manual_adjudication',
			mutatesManifest: false,
			auxiliarySignalsAffectClassification: false,
			missingCapabilityCandidateEffect: 'none_until_manual_decision'
		},
		capabilityDetectorCoverage: {
			frozenCapabilities: [ ...frozenCapabilities ].sort(),
			exactSignalCapabilities: [ ...new Set( MISSING_CAPABILITY_RULES.map( function readRuleCapability( rule ) { return rule.capability; } ) ) ].sort(),
			manualOnlyCapabilities: [ ...frozenCapabilities ].filter( function lacksExactRule( capability ) {

				return ! MISSING_CAPABILITY_RULES.some( function matchesCapability( rule ) { return rule.capability === capability; } );

			} ).sort()
		},
		counts: countAuditResults( auditedExamples ),
		examples: auditedExamples
	};

}

/** Validates structural and conservative-classification invariants of an audit document. */
export function validateCapabilityAudit( audit, manifest, expectedAuditPopulation, upstreamRoot = null ) {

	const failures = [];
	const expectedExamples = manifest.examples.filter( function isAuditCandidate( example ) { return example.status !== 'excluded_upstream'; } );
	const expectedExamplesById = new Map( expectedExamples.map( function createExamplePair( example ) { return [ example.id, example ]; } ) );
	if ( audit.schemaVersion !== 1 ) failures.push( 'schemaVersion must be 1.' );
	if ( audit.policy?.mutatesManifest !== false ) failures.push( 'Audit policy must prohibit manifest mutation.' );
	if ( audit.examples.length !== expectedAuditPopulation ) failures.push( `Expected ${ expectedAuditPopulation } audited examples, received ${ audit.examples.length }.` );
	const expectedIds = new Set( expectedExamples.map( function readExampleId( example ) { return example.id; } ) );
	const seenIds = new Set();
	const evidenceSourceCache = new Map();
	for ( const example of audit.examples ) {

		if ( ! expectedIds.has( example.id ) ) failures.push( `${ example.id }: not present in manifest audit population.` );
		if ( seenIds.has( example.id ) ) failures.push( `${ example.id }: duplicate audit entry.` );
		seenIds.add( example.id );
		if ( example.manifestStatusAtAudit !== expectedExamplesById.get( example.id )?.status ) failures.push( `${ example.id }: manifest status snapshot is stale.` );
		if ( example.classification?.decision !== 'manual_review_pending' || example.classification?.proposedStatus !== null ) failures.push( `${ example.id }: static audit must not classify the case.` );
		if ( example.missingLocalModules.length > 0 ) failures.push( `${ example.id }: ${ example.missingLocalModules.length } local module dependencies are missing.` );
		for ( const candidate of example.missingCapabilityCandidates ) {

			if ( candidate.detectorStrength !== 'exact_signal' || candidate.manualDecision !== 'pending' || candidate.statusEffect !== 'none' ) failures.push( `${ example.id }: capability candidate '${ candidate.capability }' bypasses exact-signal manual adjudication.` );
			if ( candidate.evidence.length === 0 ) failures.push( `${ example.id }: capability candidate '${ candidate.capability }' has no source evidence.` );

		}
		for ( const signal of example.auxiliaryMissingCapabilitySignals ) {

			if ( signal.detectorStrength !== 'exact_dependency_signal' || signal.coreConfidence !== 'auxiliary_only' || signal.manualDecision !== 'pending' || signal.statusEffect !== 'none' ) failures.push( `${ example.id }: auxiliary capability signal '${ signal.capability }' affects classification.` );

		}
		for ( const trigger of Object.values( example.renderSetAudit.triggers ) ) {

			if ( trigger.manualDecision !== 'pending' ) failures.push( `${ example.id }: RenderSet trigger bypasses manual adjudication.` );

		}
		for ( const dependency of example.localModules ) {

			if ( dependency.evidence.sourcePath !== dependency.importedBy ) failures.push( `${ example.id }: dependency evidence source does not match importedBy for ${ dependency.sourcePath }.` );
			const expectedRole = dependency.importedBy === example.upstreamPath ? 'example_source' : 'local_dependency';
			if ( dependency.evidence.role !== expectedRole ) failures.push( `${ example.id }: dependency evidence role is invalid for ${ dependency.sourcePath }.` );

		}
		if ( upstreamRoot !== null ) {

			const evidenceRecords = [];
			collectEmbeddedEvidence( example, evidenceRecords );
			const evidenceFailures = new Set();
			for ( const evidence of evidenceRecords ) {

				const locationFailure = validateEvidenceLocation( upstreamRoot, evidence, evidenceSourceCache );
				if ( locationFailure !== null ) evidenceFailures.add( locationFailure );

			}
			for ( const evidenceFailure of evidenceFailures ) failures.push( `${ example.id }: ${ evidenceFailure }` );

		}

	}
	for ( const expectedId of expectedIds ) {

		if ( ! seenIds.has( expectedId ) ) failures.push( `${ expectedId }: missing audit entry.` );

	}
	if ( JSON.stringify( audit.counts ) !== JSON.stringify( countAuditResults( audit.examples ) ) ) failures.push( 'Stored audit counts are stale.' );
	return failures;

}

/** Serializes the audit document deterministically with one reviewable line per example. */
export function serializeCapabilityAudit( audit ) {

	const header = { ...audit };
	delete header.examples;
	const headerLines = JSON.stringify( header, null, 2 ).split( '\n' );
	headerLines.pop();
	const exampleLines = audit.examples.map( function serializeExample( example, index ) {

		return `    ${ JSON.stringify( example ) }${ index + 1 === audit.examples.length ? '' : ',' }`;

	} );
	return `${ headerLines.join( '\n' ) },\n  "examples": [\n${ exampleLines.join( '\n' ) }\n  ]\n}\n`;

}

/** Writes an audit artifact or verifies byte-for-byte reproducibility in check mode. */
export function writeOrCheckCapabilityAudit( outputPath, serializedAudit, checkOnly ) {

	if ( checkOnly ) {

		if ( ! fs.existsSync( outputPath ) || fs.readFileSync( outputPath, 'utf8' ) !== serializedAudit ) throw new Error( `${ outputPath } is stale. Regenerate it with audit_three_capabilities.mjs.` );
		return 'checked';

	}
	fs.mkdirSync( path.dirname( outputPath ), { recursive: true } );
	fs.writeFileSync( outputPath, serializedAudit );
	return 'written';

}
