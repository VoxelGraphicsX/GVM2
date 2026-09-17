import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import { validateGeometryGroupSceneRootEvidence } from './validate_geometry_group_scene_root_evidence.mjs';

const toolsDirectory = path.dirname( fileURLToPath( import.meta.url ) );
const repositoryRoot = path.resolve( toolsDirectory, '..', '..' );
const manifestDirectory = path.join( repositoryRoot, 'GVMRuntime_ThreeSamples', 'Manifest' );
const upstreamRoot = path.join( repositoryRoot, 'build', 'three-r185-upstream' );
const evidencePath = path.join( manifestDirectory, 'three-r185-geometry-group-scene-roots.json' );
const decisionsPath = path.join( manifestDirectory, 'three-r185-builtin-geometry-group-decisions.json' );
const evidenceDocument = JSON.parse( fs.readFileSync( evidencePath, 'utf8' ) );
const decisions = JSON.parse( fs.readFileSync( decisionsPath, 'utf8' ) );

test( 'canonical grouped-root evidence covers all 58 corrections and 10 exclusions', () => {

	const result = validateGeometryGroupSceneRootEvidence( { evidenceDocument, decisions, upstreamRoot, decisionsPath } );
	assert.deepEqual( result, {
		correctedReviews: 58,
		correctedGroupedSceneRoots: 60,
		excludedCandidates: 10,
		sourceFiles: 65
	} );

} );

test( 'canonical grouped-root evidence rejects source hash drift', () => {

	const driftedEvidence = structuredClone( evidenceDocument );
	driftedEvidence.sourceFiles[ 0 ].sha256 = '0'.repeat( 64 );
	assert.throws( function validatesDriftedEvidence() {

		validateGeometryGroupSceneRootEvidence( { evidenceDocument: driftedEvidence, decisions, upstreamRoot, decisionsPath } );

	}, /sourceFiles hash is stale/u );

} );

test( 'canonical grouped-root evidence rejects a changed multi-Scene mapping', () => {

	const driftedEvidence = structuredClone( evidenceDocument );
	const exporter = driftedEvidence.geometryGroupSceneRootEvidence.find( function matchesExporter( record ) { return record.id === 'misc_exporter_gltf'; } );
	exporter.sceneRoots = [ 'scene1' ];
	assert.throws( function validatesChangedRoot() {

		validateGeometryGroupSceneRootEvidence( { evidenceDocument: driftedEvidence, decisions, upstreamRoot, decisionsPath } );

	}, /does not match its canonical decision/u );

} );
