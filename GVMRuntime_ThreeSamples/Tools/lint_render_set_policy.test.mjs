import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import {
	lintRenderSetPolicy,
	readGeometryGroupSceneRootEvidence
} from './lint_render_set_policy.mjs';

const toolsDirectory = path.dirname( fileURLToPath( import.meta.url ) );
const manifest = JSON.parse( fs.readFileSync( path.join( toolsDirectory, '..', 'Manifest', 'three-r185-manifest.json' ), 'utf8' ) );

test( 'root-scoped evidence catches a grouped second Scene hidden by an example-wide true flag', () => {

	const candidateManifest = structuredClone( manifest );
	const example = candidateManifest.examples.find( function matchesExporter( candidate ) { return candidate.id === 'misc_exporter_gltf'; } );
	const scene2 = example.sceneRoots.find( function matchesSecondScene( sceneRoot ) { return sceneRoot.name === 'scene2'; } );
	scene2.renderSetRuntimeInstanceCount = 0;
	scene2.renderSetType = null;
	const scene2Pass = example.scenePasses.find( function matchesSecondScenePass( scenePass ) { return scenePass.sceneRoot === 'scene2'; } );
	scene2Pass.renderSetBindingCount = 0;
	scene2Pass.usesStandaloneGeometry = true;
	scene2Pass.usesExplicitDrawCount = true;
	const evidence = new Map( [ [ 'misc_exporter_gltf', [ 'scene1', 'scene2' ] ] ] );
	const result = lintRenderSetPolicy( candidateManifest, false, { geometryGroupSceneRootsByExample: evidence } );
	assert.ok( result.failures.includes( "misc_exporter_gltf: Grouped Scene root 'scene2' must own exactly one RenderSet runtime instance." ) );

} );

test( 'root-scoped evidence accepts two grouped roots only after both use RenderSet-only Scene passes', () => {

	const candidateManifest = structuredClone( manifest );
	const example = candidateManifest.examples.find( function matchesExporter( candidate ) { return candidate.id === 'misc_exporter_gltf'; } );
	const scene2 = example.sceneRoots.find( function matchesSecondScene( sceneRoot ) { return sceneRoot.name === 'scene2'; } );
	scene2.renderSetRuntimeInstanceCount = 1;
	scene2.renderSetType = example.renderSetType;
	const scene2Pass = example.scenePasses.find( function matchesSecondScenePass( scenePass ) { return scenePass.sceneRoot === 'scene2'; } );
	scene2Pass.renderSetBindingCount = 1;
	scene2Pass.usesStandaloneGeometry = false;
	scene2Pass.usesExplicitDrawCount = false;
	const evidence = new Map( [ [ example.id, [ 'scene1', 'scene2' ] ] ] );
	const result = lintRenderSetPolicy( candidateManifest, false, { geometryGroupSceneRootsByExample: evidence } );
	assert.deepEqual( result.failures, [] );

} );

test( 'root-scoped evidence independently requires the global compatibility fields', () => {

	const candidateManifest = structuredClone( manifest );
	const example = candidateManifest.examples.find( function matchesMdd( candidate ) { return candidate.id === 'webgl_loader_mdd'; } );
	example.containsGeometryGroups = false;
	example.renderSetReasons = example.renderSetReasons.filter( function excludesGeometryGroups( reason ) { return reason !== 'geometry_groups'; } );
	const evidence = new Map( [ [ 'webgl_loader_mdd', [ 'scene' ] ] ] );
	const result = lintRenderSetPolicy( candidateManifest, false, { geometryGroupSceneRootsByExample: evidence } );
	assert.ok( result.failures.includes( 'webgl_loader_mdd: Root-scoped geometry-group evidence requires containsGeometryGroups=true.' ) );
	assert.ok( result.failures.includes( "webgl_loader_mdd: Root-scoped geometry-group evidence requires renderSetReason 'geometry_groups'." ) );

} );

test( 'proposal evidence parser rejects duplicate example records', () => {

	assert.throws( function readsDuplicateEvidence() {

		readGeometryGroupSceneRootEvidence( {
			geometryGroupSceneRootEvidence: [
				{ id: 'webgl_loader_mdd', sceneRoots: [ 'scene' ] },
				{ id: 'webgl_loader_mdd', sceneRoots: [ 'scene' ] }
			]
		} );

	}, /Geometry-group evidence repeats webgl_loader_mdd/u );

} );
