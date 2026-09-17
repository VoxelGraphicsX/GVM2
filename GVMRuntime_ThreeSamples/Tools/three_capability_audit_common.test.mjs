import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import {
	buildCapabilityAudit,
	createCapabilityAuditInputManifest,
	extractModuleSpecifiers,
	extractSceneRoots,
	resolveLocalModuleSpecifier,
	scanMissingCapabilityCandidates,
	scanSourceFeatures,
	serializeCapabilityAudit,
	validateCapabilityAudit,
	writeOrCheckCapabilityAudit
} from './three_capability_audit_common.mjs';

const toolsDirectory = path.dirname( fileURLToPath( import.meta.url ) );
const fixtureRoot = path.join( toolsDirectory, 'testdata', 'three-capability-audit' );
const fixtureSourcePath = 'examples/sample_complex.html';
const fixtureSourceText = fs.readFileSync( path.join( fixtureRoot, fixtureSourcePath ), 'utf8' );
const frozenCapabilities = [
	'multisample_resolve',
	'complete_stencil_operations_and_reference',
	'dsl_texture_cube',
	'dsl_texture_cube_array',
	'automatic_mipmap_generation',
	'front_facing_builtin',
	'point_size_builtin',
	'sample_mask_builtin',
	'blend_constant',
	'occlusion_query',
	'new_shader_stage',
	'render_set_filter',
	'render_set_sort',
	'render_set_reorder',
	'render_set_per_entity_pipeline',
	'new_render_set_component_type',
	'new_rhi_descriptor_encoder_or_query_api'
];

/** Creates the smallest manifest needed to exercise core and excluded audit population handling. */
function createFixtureManifest() {

	return {
		upstream: { release: 'r185', commit: 'fixture-commit' },
		examples: [
			{ id: 'sample_complex', category: 'fixture', upstreamPath: fixtureSourcePath, status: 'audit_pending' },
			{ id: 'excluded_sample', category: 'fixture', upstreamPath: 'examples/excluded_sample.html', status: 'excluded_upstream' }
		]
	};

}

test( 'module extraction ignores commented imports and resolves the recursive local graph', function testModuleExtraction() {

	const imports = extractModuleSpecifiers( fixtureSourcePath, fixtureSourceText );
	assert.deepEqual( imports.map( function readSpecifier( record ) { return record.specifier; } ), [ './jsm/fixture/SceneFactory.js' ] );
	assert.equal( imports[ 0 ].evidence.line, 5 );
	assert.equal( resolveLocalModuleSpecifier( fixtureRoot, fixtureSourcePath, imports[ 0 ].specifier ), 'examples/jsm/fixture/SceneFactory.js' );

} );

test( 'feature evidence distinguishes exact core lines from dependency signals', function testFeatureEvidence() {

	const features = scanSourceFeatures( fixtureSourcePath, fixtureSourceText );
	assert.equal( features.get( 'instancing' )[ 0 ].line, 14 );
	assert.equal( features.get( 'geometry_groups' )[ 0 ].line, 11 );
	assert.equal( features.get( 'hierarchy' ).some( function hasGroupAdd( evidence ) { return evidence.line === 15; } ), true );
	assert.equal( features.get( 'dynamic_objects' ).some( function hasModelRemove( evidence ) { return evidence.line === 19; } ), true );
	assert.equal( features.get( 'scene_addition' ).length, 0 );

} );

test( 'scene root extraction reports the assigned root and exact line', function testSceneRoots() {

	const roots = extractSceneRoots( fixtureSourcePath, fixtureSourceText );
	assert.equal( roots.length, 1 );
	assert.equal( roots[ 0 ].name, 'scene' );
	assert.equal( roots[ 0 ].evidence.line, 8 );

} );

test( 'frozen capability matches remain pending evidence and ignore comments', function testCapabilityCandidates() {

	const candidates = scanMissingCapabilityCandidates( fixtureSourcePath, fixtureSourceText, new Set( frozenCapabilities ) );
	assert.deepEqual( candidates.map( function readCapability( candidate ) { return candidate.capability; } ), [ 'front_facing_builtin', 'multisample_resolve' ] );
	for ( const candidate of candidates ) {

		assert.equal( candidate.manualDecision, 'pending' );
		assert.equal( candidate.statusEffect, 'none' );

	}

} );

test( 'multisample detection covers inline RenderTarget options without matching unrelated sample counts', function testInlineRenderTargetSamples() {

	const sourceText = [
		"const target = new THREE.WebGLRenderTarget( width, height, { type: THREE.HalfFloatType, samples: 4 } );",
		"const aoParameters = { samples: 16 };"
	].join( '\n' );
	const candidates = scanMissingCapabilityCandidates( 'examples/inline_samples.html', sourceText, new Set( frozenCapabilities ) );
	const multisampleCandidates = candidates.filter( function isMultisample( candidate ) { return candidate.capability === 'multisample_resolve'; } );
	assert.equal( multisampleCandidates.length, 1 );
	assert.equal( multisampleCandidates[ 0 ].evidence.length, 1 );
	assert.equal( multisampleCandidates[ 0 ].evidence[ 0 ].line, 1 );

} );

test( 'complete audit traverses dependencies without classifying or mutating the manifest', function testCompleteAudit() {

	const manifest = createFixtureManifest();
	const manifestText = `${ JSON.stringify( manifest ) }\n`;
	const freeze = { unsupportedPublicCapabilities: frozenCapabilities };
	const freezeText = `${ JSON.stringify( freeze ) }\n`;
	const before = JSON.stringify( manifest );
	const audit = buildCapabilityAudit( fixtureRoot, manifest, freeze, manifestText, freezeText );
	assert.equal( JSON.stringify( manifest ), before );
	assert.equal( audit.examples.length, 1 );
	assert.deepEqual( audit.examples[ 0 ].localModules.map( function readPath( dependency ) { return dependency.sourcePath; } ), [
		'examples/jsm/fixture/SceneFactory.js',
		'examples/jsm/fixture/Nested.js'
	] );
	assert.equal( audit.examples[ 0 ].features.compute.auxiliaryDetected, true );
	assert.equal( audit.examples[ 0 ].classification.proposedStatus, null );
	assert.deepEqual( validateCapabilityAudit( audit, manifest, 1, fixtureRoot ), [] );
	assert.equal( serializeCapabilityAudit( audit ), serializeCapabilityAudit( buildCapabilityAudit( fixtureRoot, manifest, freeze, manifestText, freezeText ) ) );

} );

test( 'audit input normalization is stable across manual phase status changes', function testStableAuditInput() {

	const pendingManifest = createFixtureManifest();
	const requiredManifest = structuredClone( pendingManifest );
	requiredManifest.examples[ 0 ].status = 'phase1_required';
	requiredManifest.examples[ 0 ].dslShard = 'FixtureShard';
	assert.deepEqual(
		createCapabilityAuditInputManifest( pendingManifest ),
		createCapabilityAuditInputManifest( requiredManifest )
	);
	assert.equal( createCapabilityAuditInputManifest( requiredManifest ).examples[ 0 ].status, 'audit_pending' );

} );

test( 'check mode rejects stale output and accepts the exact deterministic bytes', function testCheckMode() {

	const temporaryDirectory = fs.mkdtempSync( path.join( os.tmpdir(), 'gvm-three-audit-' ) );
	const outputPath = path.join( temporaryDirectory, 'audit.json' );
	assert.throws( function checkMissingOutput() { writeOrCheckCapabilityAudit( outputPath, 'expected\n', true ); }, /is stale/ );
	assert.equal( writeOrCheckCapabilityAudit( outputPath, 'expected\n', false ), 'written' );
	assert.equal( writeOrCheckCapabilityAudit( outputPath, 'expected\n', true ), 'checked' );
	fs.writeFileSync( outputPath, 'stale\n' );
	assert.throws( function checkStaleOutput() { writeOrCheckCapabilityAudit( outputPath, 'expected\n', true ); }, /is stale/ );
	fs.rmSync( temporaryDirectory, { recursive: true, force: true } );

} );
