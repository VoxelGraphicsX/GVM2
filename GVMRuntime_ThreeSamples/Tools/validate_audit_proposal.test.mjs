import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import {
	parseAuditProposalArguments,
	validateAuditProposal
} from './validate_audit_proposal.mjs';

const toolsDirectory = path.dirname( fileURLToPath( import.meta.url ) );
const repositoryRoot = path.resolve( toolsDirectory, '..', '..' );
const manifestDirectory = path.resolve( toolsDirectory, '..', 'Manifest' );
const upstreamRoot = path.join( repositoryRoot, 'build', 'three-r185-lock-upstream' );

/** Reads one checked-in JSON input used by the proposal validator. */
function readJson( fileName ) {

	return JSON.parse( fs.readFileSync( path.join( manifestDirectory, fileName ), 'utf8' ) );

}

/** Creates one full-entry proposal that exercises ordered multi-camera Scene submissions. */
function createProposal() {

	const assetPath = path.join( upstreamRoot, 'examples', 'textures', 'crate.gif' );
	const assetSha256 = crypto.createHash( 'sha256' ).update( fs.readFileSync( assetPath ) ).digest( 'hex' );
	return {
		schemaVersion: 1,
		proposalKind: 'three-r185-phase1-capability-and-render-set-review',
		upstreamCommit: '2431a09f46f34c560bc8e44b33be0e567723d5b9',
		scope: {
			auditedPendingExamples: 1,
			phase1Required: 1,
			deferredMissingCapability: 0
		},
		proposals: [ {
			id: 'webgl_camera',
			upstreamPath: 'examples/webgl_camera.html',
			proposedStatus: 'phase1_required',
			reviewScope: 'complete_entry_html_recursive_camera_helpers_and_scene_topology',
			upstreamSourceEvidence: [
				{ sourcePath: 'examples/webgl_camera.html', line: 56, excerpt: 'scene = new THREE.Scene();' },
				{ sourcePath: 'examples/webgl_camera.html', line: 140, excerpt: 'renderer.setScissorTest( true );' }
			],
			repositoryCapabilityEvidence: [
				{ sourcePath: 'GVMRuntime_ThreeSamples/Dsl/RenderSetPhase0/RenderSetPhase0.hpp', line: 78, excerpt: 'uint renderEntityID [[RenderEntityID]]' }
			],
			requiredCapabilities: [ 'render_set_indexed_indirect', 'triangle_list_helper_expansion', 'viewport_scissor' ],
			availableCapabilities: [ 'render_set_indexed_indirect', 'triangle_list_helper_expansion', 'viewport_scissor' ],
			missingCapabilities: [],
			notes: [ 'The same Scene RenderSet is submitted twice with deterministic perspective and orthographic camera data.' ],
			renderSetPolicy: 'required',
			renderSetReasons: [ 'multiple_renderables', 'hierarchy', 'multiple_materials' ],
			sceneRoots: [ { name: 'scene', renderSetRuntimeInstanceCount: 1, renderSetType: 'WebglCameraSceneRenderSet' } ],
			renderableObjectCount: 6,
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
			],
			dslShard: 'Phase1CameraRenderSet',
			scenarios: [
				{
					id: 'initial',
					kind: 'initial-frame',
					frame: 0,
					inputReplay: null,
					canonicalState: 'perspective-left-orthographic-right',
					scenePassInvocations: [ { sceneRoot: 'scene', scenePass: 'main', invocationCount: 2 } ],
					scenePassSequence: [
						{ sceneRoot: 'scene', scenePass: 'main', entityOrdinal: 0 },
						{ sceneRoot: 'scene', scenePass: 'main', entityOrdinal: 1 }
					]
				},
				{ id: 'animated', kind: 'fixed-frame', frame: 60, inputReplay: null, canonicalState: 'fixed-step-camera-rig' }
			],
			canonicalAssetTopology: {
				assets: [ { path: 'examples/textures/crate.gif', sha256: assetSha256 } ]
			},
			externalAssetRequirements: {
				schemaVersion: 1,
				mappings: [ {
					type: 'exact',
					url: 'https://example.invalid/fixture/crate.gif',
					assetPackPath: 'external/fixture/crate.gif',
					sha256: assetSha256,
					byteSize: fs.statSync( assetPath ).size,
					mimeType: 'image/gif'
				} ]
			},
			deferredEvidence: null
		} ]
	};

}

/** Creates the immutable checked-in inputs shared by each proposal validation assertion. */
function createInputs( proposal ) {

	const statusLock = readJson( 'three-r185-phase1-status-lock.json' );
	statusLock.phase1RequiredReviews = statusLock.phase1RequiredReviews.filter( function keepsOtherReviews( review ) {

		return review.id !== 'webgl_camera';

	} );
	return {
		proposal,
		upstreamRoot,
		inventory: readJson( 'upstream-files-r185.json' ),
		exclusions: readJson( 'three-r185-exclusions.json' ),
		capabilityAudit: readJson( 'three-r185-capability-audit.json' ),
		adjudication: readJson( 'three-r185-capability-adjudication.json' ),
		statusLock,
		manifestSchema: readJson( 'three-r185-manifest.schema.json' ),
		geometryGroupSceneRootsByExample: new Map()
	};

}

test( 'proposal gate validates source evidence, assets, manifest schema, and RenderSet policy before merge', () => {

	const result = validateAuditProposal( createInputs( createProposal() ) );
	assert.equal( result.proposalCount, 1 );
	assert.equal( result.phase1Required, 1 );
	assert.equal( result.deferredMissingCapability, 0 );
	assert.equal( result.replacedLocked, 0 );
	assert.equal( result.pendingAfterMerge, readJson( 'three-r185-manifest.json' ).counts.auditPending );
	assert.equal( result.normalizedRequired[ 0 ].assetEvidence.length, 1 );
	assert.match( result.normalizedRequired[ 0 ].assetEvidence[ 0 ].gitBlobSha1, /^[a-f0-9]{40}$/u );
	assert.deepEqual( result.normalizedRequired[ 0 ].externalAssetRequirements, createProposal().proposals[ 0 ].externalAssetRequirements );

} );

test( 'proposal gate explicitly validates same-status corrections to locked reviews', () => {

	const inputs = createInputs( createProposal() );
	inputs.statusLock = readJson( 'three-r185-phase1-status-lock.json' );
	inputs.replaceLocked = true;
	const result = validateAuditProposal( inputs );
	assert.equal( result.replacedLocked, 1 );
	assert.equal( result.pendingAfterMerge, 0 );

	const missingLockedReview = createInputs( createProposal() );
	missingLockedReview.replaceLocked = true;
	assert.throws( () => validateAuditProposal( missingLockedReview ), /has no locked Phase-1 review to replace/u );

} );

test( 'proposal gate rejects stale evidence, stale counts, and already locked cases', () => {

	const staleEvidence = createProposal();
	staleEvidence.proposals[ 0 ].upstreamSourceEvidence[ 0 ].line = 57;
	assert.throws( () => validateAuditProposal( createInputs( staleEvidence ) ), /does not match/u );

	const staleCounts = createProposal();
	staleCounts.scope.phase1Required = 0;
	assert.throws( () => validateAuditProposal( createInputs( staleCounts ) ), /phase1Required count is stale/u );

	const alreadyLocked = createProposal();
	alreadyLocked.proposals[ 0 ].id = 'webgl_geometry_cube';
	alreadyLocked.proposals[ 0 ].upstreamPath = 'examples/webgl_geometry_cube.html';
	assert.throws( () => validateAuditProposal( createInputs( alreadyLocked ) ), /already has a locked Phase-1 status/u );

	const missingExternalMappings = createProposal();
	missingExternalMappings.proposals[ 0 ].externalAssetRequirements.mappings = [];
	assert.throws( () => validateAuditProposal( createInputs( missingExternalMappings ) ), /externalAssetRequirements\.mappings must be a non-empty array/u );

} );

test( 'proposal CLI parsing requires explicit source paths and rejects unknown configuration', () => {

	assert.throws( () => parseAuditProposalArguments( [] ), /--proposal and --upstream-root are required/u );
	assert.throws( () => parseAuditProposalArguments( [ '--proposal', 'proposal.json', '--upstream-root', 'upstream', '--mystery', 'x' ] ), /Unknown argument/u );
	const options = parseAuditProposalArguments( [ '--proposal', 'proposal.json', '--upstream-root', 'upstream', '--replace-locked' ] );
	assert.equal( options.proposalPath, path.resolve( 'proposal.json' ) );
	assert.equal( options.upstreamRoot, path.resolve( 'upstream' ) );
	assert.equal( options.replaceLocked, true );

} );
