import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import { applySingleSamplePolicy, buildManifest } from './generate_manifest.mjs';
import { THREE_R185_COMMIT, serializeJson } from './manifest_common.mjs';
import { buildPhase1ManifestLock } from './phase1_manifest_lock_common.mjs';
import { validateStatusLock } from './lock_phase1_manifest.mjs';

const toolsDirectory = path.dirname( fileURLToPath( import.meta.url ) );
const repositoryRoot = path.resolve( toolsDirectory, '..', '..' );
const manifestDirectory = path.resolve( toolsDirectory, '..', 'Manifest' );

const expectedRequiredIds = [
	'webgl_geometry_cube',
	'webgl_shader',
	'webgl_buffergeometry_attributes_none',
	'webgl_materials_texture_canvas',
	'webgpu_compute_texture'
];
const expectedDetectorDeferredIds = [
	'webgl_clipping_stencil',
	'webgl_lines_fat',
	'webgl_materials_wireframe',
	'webgl_multiple_rendertargets',
	'webgl_multisampled_renderbuffers',
	'webgpu_multisampled_renderbuffers',
	'webgpu_occlusion',
	'webgpu_lines_fat'
];

/** Reads one checked-in phase-1 JSON document. */
function readManifestJson( fileName ) {

	return JSON.parse( fs.readFileSync( path.join( manifestDirectory, fileName ), 'utf8' ) );

}

/** Returns a sorted copy without mutating the source list. */
function sorted( values ) {

	return [ ...values ].sort();

}

/** Returns one generated manifest example and fails clearly when it is missing. */
function requireExample( exampleId ) {

	const example = generatedLock.manifest.examples.find( function matchesId( candidate ) { return candidate.id === exampleId; } );
	assert.ok( example, `Missing manifest example ${ exampleId }.` );
	return example;

}

const inventory = readManifestJson( 'upstream-files-r185.json' );
const exclusions = readManifestJson( 'three-r185-exclusions.json' );
const capabilityAudit = readManifestJson( 'three-r185-capability-audit.json' );
const adjudication = readManifestJson( 'three-r185-capability-adjudication.json' );
const statusLock = readManifestJson( 'three-r185-phase1-status-lock.json' );
const expectedDeferredIds = [
	...expectedDetectorDeferredIds,
	...statusLock.phase1DeferredReviews.map( function mapId( review ) { return review.id; } ),
	...statusLock.phase1DeferredReclassifications.map( function mapId( review ) { return review.id; } )
];
const checkedManifest = readManifestJson( 'three-r185-manifest.json' );
const checkedReport = readManifestJson( 'three-r185-phase1-lock-report.json' );
const baselineManifest = buildManifest( inventory, exclusions );
const rawGeneratedLock = buildPhase1ManifestLock( baselineManifest, capabilityAudit, adjudication, statusLock );
const generatedLock = {
	...rawGeneratedLock,
	manifest: applySingleSamplePolicy( rawGeneratedLock.manifest )
};

test( 'phase-1 status lock accounting matches every explicit required and deferred decision', () => {

	const reclassifiedIds = new Set( statusLock.phase1DeferredReclassifications.map( function mapId( review ) { return review.id; } ) );
	const lockedRequiredIds = statusLock.phase1RequiredReviews.filter( function isNotReclassified( review ) {

		return ! reclassifiedIds.has( review.id );

	} ).map( function mapId( review ) { return review.id; } );
	assert.deepEqual( generatedLock.manifest.counts, {
		total: 588,
		excludedUpstream: 77,
		auditPopulation: 511,
		auditPending: 511 - lockedRequiredIds.length - expectedDeferredIds.length,
		phase1Required: lockedRequiredIds.length,
		deferredMissingCapability: expectedDeferredIds.length
	} );
	const requiredExamples = generatedLock.manifest.examples.filter( function isRequired( example ) { return example.status === 'phase1_required'; } );
	const deferredExamples = generatedLock.manifest.examples.filter( function isDeferred( example ) { return example.status === 'deferred_missing_capability'; } );
	assert.deepEqual( sorted( requiredExamples.map( function mapId( example ) { return example.id; } ) ), sorted( lockedRequiredIds ) );
	assert.deepEqual( sorted( deferredExamples.map( function mapId( example ) { return example.id; } ) ), sorted( expectedDeferredIds ) );
	assert.deepEqual( generatedLock.report.lockedPhase1RequiredIds, lockedRequiredIds );
	assert.deepEqual( sorted( generatedLock.report.lockedDeferredIds ), sorted( expectedDeferredIds ) );
	assert.ok( expectedRequiredIds.every( function keepsImplementedSlice( exampleId ) { return lockedRequiredIds.includes( exampleId ); } ) );

	for ( const example of requiredExamples ) {

		assert.equal( example.capabilityAudit.state, 'supported' );
		assert.equal( example.capabilityAudit.requiresNewPublicCapability, false );
		assert.equal( example.deferredEvidence, null );
		assert.ok( example.dslShard );
		assert.ok( example.scenarios.length > 0 );

	}
	for ( const example of deferredExamples ) {

		assert.equal( example.capabilityAudit.state, 'deferred' );
		assert.equal( example.capabilityAudit.requiresNewPublicCapability, true );
		assert.ok( example.deferredEvidence );
		assert.ok( example.deferredEvidence.reasonCode );
		assert.ok( example.deferredEvidence.upstreamSourceEvidence.length > 0 );
		assert.ok( example.deferredEvidence.missingCapability );
		assert.ok( example.deferredEvidence.whyCurrentDslCannotExpressIt );
		assert.ok( example.deferredEvidence.minimumFutureApi );
		assert.ok( example.deferredEvidence.reentryTest );

	}

} );

test( 'MRT topology remains a one-object Scene plus exempt fullscreen composite without RenderSet', () => {

	const example = requireExample( 'webgl_multiple_rendertargets' );
	assert.equal( example.status, 'deferred_missing_capability' );
	assert.equal( example.renderSetPolicy, 'not-required' );
	assert.deepEqual( example.renderSetReasons, [] );
	assert.equal( example.renderableObjectCount, 1 );
	assert.deepEqual( example.sceneRoots.map( function mapRoot( root ) { return root.name; } ), [ 'scene', 'postScene' ] );
	assert.ok( example.sceneRoots.every( function hasNoRenderSet( root ) { return root.renderSetRuntimeInstanceCount === 0 && root.renderSetType === null; } ) );
	assert.deepEqual( example.scenePasses, [ {
		name: 'mrt-scene',
		renderClass: 'WebglMultipleRendertargetsMrtScenePass',
		sceneRoot: 'scene',
		renderSetBindingCount: 0,
		usesStandaloneGeometry: true,
		usesExplicitDrawCount: true
	} ] );
	assert.deepEqual( example.screenPasses, [ 'mrt-composite' ] );
	assert.equal( example.renderSetType, null );

} );

test( 'deferred WebGL multisample comparison still locks one Scene RenderSet across both geometry passes', () => {

	const example = requireExample( 'webgl_multisampled_renderbuffers' );
	assert.equal( example.status, 'deferred_missing_capability' );
	assert.equal( example.renderSetPolicy, 'required' );
	assert.equal( example.renderableObjectCount, 100 );
	assert.deepEqual( example.sceneRoots, [ {
		name: 'scene',
		renderSetRuntimeInstanceCount: 1,
		renderSetType: 'WebglMultisampledRenderbuffersSceneRenderSet'
	} ] );
	assert.deepEqual( example.scenePasses.map( function mapPass( pass ) { return pass.name; } ), [
		'single-sample-left',
		'multisample-right'
	] );
	assert.ok( example.scenePasses.every( function usesTheSceneSet( pass ) {

		return pass.renderSetBindingCount === 1 && pass.usesStandaloneGeometry === false && pass.usesExplicitDrawCount === false;

	} ) );
	assert.deepEqual( example.componentSchema.slice( 0, 5 ).map( function mapRole( component ) { return component.role; } ), [
		'vertex', 'index', 'object', 'instance', 'material'
	] );

} );

test( 'point-size expansion locks instancing or explicit vertices while preserving independent RenderSet triggers', () => {

	const pointExpansionIds = adjudication.decisions.filter( function isPointExpansion( decision ) {

		return decision.capability === 'point_size_builtin' && decision.decision === 'expressible_existing_dsl';

	} ).map( function mapId( decision ) { return decision.exampleId; } );
	assert.equal( pointExpansionIds.length, 7 );
	const pendingPointExpansionIds = pointExpansionIds.filter( function isPendingPointExpansion( exampleId ) {

		return requireExample( exampleId ).status === 'audit_pending';

	} );
	assert.deepEqual(
		sorted( generatedLock.report.blockers.derived_point_billboard_instancing_requires_RenderEntityInstanceID?.exampleIds ?? [] ),
		sorted( pendingPointExpansionIds )
	);
	for ( const exampleId of pendingPointExpansionIds ) {

		const example = requireExample( exampleId );
		assert.equal( example.status, 'audit_pending' );
		assert.equal( example.renderSetPolicy, 'required' );
		assert.equal( example.containsInstancing, true );
		assert.ok( example.renderSetReasons.includes( 'instancing' ) );
		assert.ok( example.capabilityAudit.evidence.some( function namesInstanceBuiltin( evidence ) {

			return evidence.includes( 'RenderEntityInstanceID' );

		} ) );
		assert.ok( example.sceneRoots.length > 0 );
		assert.ok( example.sceneRoots.every( function hasOneRenderSet( root ) { return root.renderSetRuntimeInstanceCount === 1; } ) );
		assert.ok( example.scenePasses.every( function bindsOneRenderSet( pass ) {

			return pass.renderSetBindingCount === 1 && pass.usesStandaloneGeometry === false && pass.usesExplicitDrawCount === false;

		} ) );

	}
	/** Returns whether one reviewed point-size equivalent uses RenderSet instance expansion. */
	function isRequiredInstanceExpansion( exampleId ) {

		const example = requireExample( exampleId );
		return example.status === 'phase1_required' && example.containsInstancing === true;

	}
	const requiredInstanceExpansionIds = pointExpansionIds.filter( isRequiredInstanceExpansion );
	assert.deepEqual( requiredInstanceExpansionIds, [ 'webgl_buffergeometry_custom_attributes_particles' ] );
	for ( const exampleId of requiredInstanceExpansionIds ) {

		const instancedExample = requireExample( exampleId );
		assert.equal( instancedExample.renderSetPolicy, 'required' );
		assert.ok( instancedExample.renderSetReasons.includes( 'instancing' ) );
		assert.ok( instancedExample.scenePasses.every( function bindsOneRenderSet( pass ) {

			return pass.renderSetBindingCount === 1 && pass.usesStandaloneGeometry === false && pass.usesExplicitDrawCount === false;

		} ) );

	}
	const expandedVertexIds = pointExpansionIds.filter( function isRequiredVertexExpansion( exampleId ) {

		const example = requireExample( exampleId );
		return example.status === 'phase1_required' && example.containsInstancing === false;

	} );
	assert.deepEqual( sorted( expandedVertexIds ), [
		'webgl_custom_attributes_points',
		'webgl_custom_attributes_points2',
		'webgl_custom_attributes_points3',
		'webgl_gpgpu_protoplanet',
		'webgl_interactive_points',
		'webgl_points_waves'
	] );
	for ( const exampleId of expandedVertexIds ) {

		const expandedVertexExample = requireExample( exampleId );
		assert.equal( expandedVertexExample.containsInstancing, false );
		assert.ok( expandedVertexExample.capabilityAudit.notes.some( function documentsVertexExpansion( note ) {

			return note.includes( 'point' ) && (
				note.includes( 'corner vertices' )
				|| note.includes( 'quad vertices' )
				|| note.includes( 'triangle-list vertices' )
			);

		} ) );

	}
	const interactivePoints = requireExample( 'webgl_interactive_points' );
	assert.equal( interactivePoints.renderSetPolicy, 'required' );
	assert.ok( interactivePoints.renderSetReasons.includes( 'geometry_groups' ) );
	assert.ok( interactivePoints.sceneRoots.every( function hasOneRenderSet( root ) { return root.renderSetRuntimeInstanceCount === 1; } ) );
	assert.ok( interactivePoints.scenePasses.every( function bindsOneRenderSet( pass ) {

		return pass.renderSetBindingCount === 1 && pass.usesStandaloneGeometry === false && pass.usesExplicitDrawCount === false;

	} ) );
	assert.equal( requireExample( 'webgl_points_waves' ).renderSetPolicy, 'not-required' );

} );

test( 'every pending example remains blocked and no blocker points at a locked status', () => {

	const pendingExamples = generatedLock.manifest.examples.filter( function isPending( example ) { return example.status === 'audit_pending'; } );
	const pendingIds = new Set( pendingExamples.map( function mapId( example ) { return example.id; } ) );
	assert.equal( pendingExamples.length, generatedLock.manifest.counts.auditPending );
	assert.deepEqual(
		generatedLock.report.blockers.full_entry_gpu_semantic_review_missing?.exampleIds ?? [],
		pendingExamples.map( function mapId( example ) { return example.id; } )
	);
	for ( const example of pendingExamples ) {

		assert.equal( example.capabilityAudit.state, 'pending' );
		assert.equal( example.deferredEvidence, null );
		assert.ok( example.capabilityAudit.notes.some( function recordsBlockers( note ) { return note.startsWith( 'Remaining lock blockers:' ); } ) );

	}
	for ( const [ blockerCode, blocker ] of Object.entries( generatedLock.report.blockers ) ) {

		assert.equal( blocker.count, blocker.exampleIds.length, `${ blockerCode } count mismatch.` );
		assert.equal( new Set( blocker.exampleIds ).size, blocker.exampleIds.length, `${ blockerCode } contains duplicate IDs.` );
		assert.ok( blocker.exampleIds.every( function isPendingId( exampleId ) { return pendingIds.has( exampleId ); } ), `${ blockerCode } references a locked status.` );

	}
	assert.equal( generatedLock.report.readyForStrictGate, pendingExamples.length === 0 );

} );

test( 'checked-in manifest and blocker report are byte-for-byte reproducible', () => {

	assert.deepEqual( generatedLock.manifest, checkedManifest );
	assert.deepEqual( generatedLock.report, checkedReport );
	assert.equal(
		serializeJson( generatedLock.manifest ),
		fs.readFileSync( path.join( manifestDirectory, 'three-r185-manifest.json' ), 'utf8' )
	);
	assert.equal(
		`${ JSON.stringify( generatedLock.report, null, 2 ) }\n`,
		fs.readFileSync( path.join( manifestDirectory, 'three-r185-phase1-lock-report.json' ), 'utf8' )
	);

} );

test( 'status lock validation rejects required reviews without complete-entry or repository evidence', ( context ) => {

	const upstreamRoot = fs.mkdtempSync( path.join( os.tmpdir(), 'gvm-three-status-lock-' ) );
	context.after( () => fs.rmSync( upstreamRoot, { recursive: true, force: true } ) );
	fs.mkdirSync( path.join( upstreamRoot, 'examples' ), { recursive: true } );
	fs.writeFileSync( path.join( upstreamRoot, 'examples', 'synthetic.html' ), 'synthetic upstream evidence\n' );
	const assetBytes = Buffer.from( 'canonical asset bytes\n' );
	fs.writeFileSync( path.join( upstreamRoot, 'examples', 'synthetic.bin' ), assetBytes );
	const assetSha256 = crypto.createHash( 'sha256' ).update( assetBytes ).digest( 'hex' );
	const assetGitBlobSha1 = crypto.createHash( 'sha1' ).update( `blob ${ assetBytes.length }\0` ).update( assetBytes ).digest( 'hex' );
	const syntheticAudit = { examples: [ { id: 'synthetic' } ] };
	const syntheticAdjudication = { decisions: [] };
	const validLock = {
		upstreamCommit: THREE_R185_COMMIT,
		policy: {
			publicCapabilityExpansionAllowed: false,
			requiredStatusNeedsCompleteEntryReview: true,
			absenceOfDetectorSignalIsSupportEvidence: false,
			workloadOrMissingLoaderImplementationCanDefer: false
		},
		phase1RequiredReviews: [ {
			id: 'synthetic',
			reviewScope: 'complete_entry_html',
			upstreamSourceEvidence: [ { sourcePath: 'examples/synthetic.html', line: 1, excerpt: 'synthetic upstream evidence' } ],
			repositoryCapabilityEvidence: [ { sourcePath: 'GVM/UGLHeaders/Details/UGL.Shaders.h', line: 130, excerpt: 'RenderPassTaskDescriptor operator()(uint vertexCount, uint instanceCount' } ],
			assetEvidence: [ {
				sourcePath: 'examples/synthetic.bin',
				gitBlobSha1: assetGitBlobSha1,
				sha256: assetSha256,
				semantic: 'Synthetic canonical asset.'
			} ],
			requiredCapabilities: [ 'vertex_render' ],
			availableCapabilities: [ 'vertex_render' ],
			notes: [ 'Synthetic complete-entry review.' ],
			renderSetPolicy: 'not-required',
			dslShard: 'Synthetic',
			scenarios: [ { id: 'initial', kind: 'initial-frame', frame: 0 } ]
		} ],
		phase1DeferredReviews: [],
		renderSetOverrides: []
	};
	assert.doesNotThrow( () => validateStatusLock( validLock, syntheticAudit, syntheticAdjudication, upstreamRoot, repositoryRoot ) );
	const missingReviewScope = structuredClone( validLock );
	delete missingReviewScope.phase1RequiredReviews[ 0 ].reviewScope;
	assert.throws( () => validateStatusLock( missingReviewScope, syntheticAudit, syntheticAdjudication, upstreamRoot, repositoryRoot ), /complete-entry review scope/ );
	const missingRepositoryEvidence = structuredClone( validLock );
	missingRepositoryEvidence.phase1RequiredReviews[ 0 ].repositoryCapabilityEvidence = [];
	assert.throws( () => validateStatusLock( missingRepositoryEvidence, syntheticAudit, syntheticAdjudication, upstreamRoot, repositoryRoot ), /lacks repository capability evidence/ );
	const staleAssetEvidence = structuredClone( validLock );
	staleAssetEvidence.phase1RequiredReviews[ 0 ].assetEvidence[ 0 ].sha256 = '0'.repeat( 64 );
	assert.throws( () => validateStatusLock( staleAssetEvidence, syntheticAudit, syntheticAdjudication, upstreamRoot, repositoryRoot ), /SHA-256 does not match/ );

	const validDeferredLock = structuredClone( validLock );
	const deferredReview = validDeferredLock.phase1RequiredReviews.pop();
	delete deferredReview.dslShard;
	delete deferredReview.scenarios;
	deferredReview.requiredCapabilities = [ 'vertex_render', 'synthetic_query' ];
	deferredReview.availableCapabilities = [ 'vertex_render' ];
	deferredReview.missingCapabilities = [ 'synthetic_query' ];
	deferredReview.reasonCode = 'deferred_missing_rhi_capability';
	deferredReview.whyCurrentDslCannotExpressIt = 'The frozen RHI has no synthetic query contract.';
	deferredReview.minimumFutureApi = 'Expose the synthetic query through the public RHI contract.';
	deferredReview.reentryTest = 'Run the same entry in all four quadrants and verify the query result.';
	validDeferredLock.phase1DeferredReviews.push( deferredReview );
	assert.doesNotThrow( () => validateStatusLock( validDeferredLock, syntheticAudit, syntheticAdjudication, upstreamRoot, repositoryRoot ) );
	const invalidDeferredReason = structuredClone( validDeferredLock );
	invalidDeferredReason.phase1DeferredReviews[ 0 ].reasonCode = 'workload_is_large';
	assert.throws( () => validateStatusLock( invalidDeferredReason, syntheticAudit, syntheticAdjudication, upstreamRoot, repositoryRoot ), /invalid deferred reasonCode/ );

} );

test( 'manual deferred reviews lock capabilities that were absent from detector adjudication', () => {

	const manualStatusLock = structuredClone( statusLock );
	manualStatusLock.phase1RequiredReviews = manualStatusLock.phase1RequiredReviews.filter( function keepsOtherRequiredReviews( review ) {

		return review.id !== 'webgl_camera';

	} );
	manualStatusLock.phase1DeferredReviews.push( {
		id: 'webgl_camera',
		reviewScope: 'complete_entry_html_recursive_scene_topology',
		upstreamSourceEvidence: [ { sourcePath: 'examples/webgl_camera.html', line: 66, excerpt: 'scene = new THREE.Scene();' } ],
		repositoryCapabilityEvidence: [ { sourcePath: 'GVM/UGLHeaders/Details/UGL.RenderSet.h', line: 167, excerpt: 'RenderEntity<T> alloc(const RenderSetAllocInfo &info)' } ],
		requiredCapabilities: [ 'render_set_indexed_indirect', 'synthetic_camera_query' ],
		availableCapabilities: [ 'render_set_indexed_indirect' ],
		missingCapabilities: [ 'synthetic_camera_query' ],
		notes: [ 'Synthetic review verifies the manual-deferred lock path.' ],
		reasonCode: 'deferred_missing_rhi_capability',
		whyCurrentDslCannotExpressIt: 'The frozen surface intentionally lacks the synthetic camera query.',
		minimumFutureApi: 'Expose a synthetic camera query without changing RenderSet ownership.',
		reentryTest: 'Render the canonical camera scenario in all four quadrants and validate the query.',
		renderSetPolicy: 'required',
		renderSetReasons: [ 'multiple_renderables', 'hierarchy' ],
		sceneRoots: [ { name: 'scene', renderSetRuntimeInstanceCount: 1, renderSetType: 'WebglCameraSceneRenderSet' } ],
		renderableObjectCount: 2,
		containsInstancing: false,
		containsHierarchy: true,
		containsLod: false,
		containsDynamicObjects: false,
		containsMultipleMaterials: true,
		containsGeometryGroups: false,
		loaderRenderableObjectCount: 0,
		scenePasses: [ {
			name: 'main',
			renderClass: 'WebglCameraMainPass',
			sceneRoot: 'scene',
			renderSetBindingCount: 1,
			usesStandaloneGeometry: false,
			usesExplicitDrawCount: false
		} ],
		screenPasses: [],
		renderSetType: 'WebglCameraSceneRenderSet',
		componentSchema: [
			{ name: 'vertices', kind: 'buffer', role: 'vertex' },
			{ name: 'indices', kind: 'buffer', role: 'index' },
			{ name: 'objects', kind: 'buffer', role: 'object' },
			{ name: 'instances', kind: 'buffer', role: 'instance' },
			{ name: 'materials', kind: 'buffer', role: 'material' }
		]
	} );
	const manualLock = buildPhase1ManifestLock( baselineManifest, capabilityAudit, adjudication, manualStatusLock );
	const example = manualLock.manifest.examples.find( function isCamera( candidate ) { return candidate.id === 'webgl_camera'; } );
	assert.equal( example.status, 'deferred_missing_capability' );
	assert.deepEqual( example.capabilityAudit.availableCapabilities, [ 'render_set_indexed_indirect' ] );
	assert.deepEqual( example.capabilityAudit.missingCapabilities, [ 'synthetic_camera_query' ] );
	assert.equal( example.deferredEvidence.reasonCode, 'deferred_missing_rhi_capability' );
	assert.equal( example.deferredEvidence.missingCapability, 'synthetic_camera_query' );
	assert.ok( manualLock.report.lockedDeferredIds.includes( 'webgl_camera' ) );

} );

test( 'deferred reclassification preserves the completed implementation contract', () => {

	const example = requireExample( 'webgl_helpers' );
	assert.equal( example.status, 'deferred_missing_capability' );
	assert.equal( example.dslShard, 'Phase1WebglHelpersRenderSet' );
	assert.equal( example.scenarios.length, 3 );
	assert.deepEqual( example.capabilityAudit.missingCapabilities,
		[ 'deterministic_cross_backend_native_line_sample_coverage' ] );
	assert.equal( example.deferredEvidence.reasonCode,
		'deferred_missing_native_line_rasterization' );
	assert.equal( example.screenPasses.length, 0 );

} );
