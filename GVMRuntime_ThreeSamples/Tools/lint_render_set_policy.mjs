#!/usr/bin/env node

import path from 'node:path';
import { fileURLToPath } from 'node:url';

import {
	defaultManifestPath,
	readJson,
	readOption
} from './manifest_common.mjs';

const toolsDirectory = path.dirname( fileURLToPath( import.meta.url ) );
const defaultGeometryGroupEvidencePath = path.join( toolsDirectory, '..', 'Manifest', 'three-r185-geometry-group-scene-roots.json' );
const REQUIRED_COMPONENT_ROLES = new Set( [ 'vertex', 'index', 'object', 'instance', 'material' ] );
let defaultGeometryGroupSceneRootsByExample = null;

/** Adds a RenderSet policy lint failure for one example. */
function addFailure( failures, example, message ) {

	failures.push( `${ example.id }: ${ message }` );

}

/** Returns every structural reason that forces one Scene to use RenderSet. */
function deriveRenderSetReasons( example ) {

	const reasons = [];
	if ( example.containsInstancing ) reasons.push( 'instancing' );
	if ( example.sceneRoots.length <= 1 && example.renderableObjectCount !== null && example.renderableObjectCount >= 2 ) reasons.push( 'multiple_renderables' );
	if ( example.containsHierarchy ) reasons.push( 'hierarchy' );
	if ( example.containsLod ) reasons.push( 'lod' );
	if ( example.containsDynamicObjects ) reasons.push( 'dynamic_objects' );
	if ( example.containsMultipleMaterials ) reasons.push( 'multiple_materials' );
	if ( example.containsGeometryGroups ) reasons.push( 'geometry_groups' );
	if ( example.sceneRoots.length <= 1 && example.loaderRenderableObjectCount !== null && example.loaderRenderableObjectCount >= 2 ) reasons.push( 'loader_multiple_renderables' );
	return reasons;

}

/** Validates independently audited grouped Scene roots without trusting the example-wide boolean. */
function lintGeometryGroupSceneRoots( example, groupedSceneRoots, sceneRootsByName, failures ) {

	if ( groupedSceneRoots.length === 0 ) return;
	if ( example.containsGeometryGroups !== true ) addFailure( failures, example, 'Root-scoped geometry-group evidence requires containsGeometryGroups=true.' );
	if ( ! example.renderSetReasons.includes( 'geometry_groups' ) ) addFailure( failures, example, "Root-scoped geometry-group evidence requires renderSetReason 'geometry_groups'." );
	const seenRootNames = new Set();
	for ( const rootName of groupedSceneRoots ) {

		if ( typeof rootName !== 'string' || rootName.length === 0 ) {

			addFailure( failures, example, 'Root-scoped geometry-group evidence contains an invalid Scene root name.' );
			continue;

		}
		if ( seenRootNames.has( rootName ) ) addFailure( failures, example, `Root-scoped geometry-group evidence repeats Scene root '${ rootName }'.` );
		seenRootNames.add( rootName );
		const sceneRoot = sceneRootsByName.get( rootName );
		if ( sceneRoot === undefined ) {

			addFailure( failures, example, `Root-scoped geometry-group evidence references unknown Scene root '${ rootName }'.` );
			continue;

		}
		if ( sceneRoot.renderSetRuntimeInstanceCount !== 1 ) addFailure( failures, example, `Grouped Scene root '${ rootName }' must own exactly one RenderSet runtime instance.` );
		const rootPasses = example.scenePasses.filter( function matchesRoot( scenePass ) { return scenePass.sceneRoot === rootName; } );
		if ( rootPasses.length === 0 ) addFailure( failures, example, `Grouped Scene root '${ rootName }' must declare at least one Scene geometry pass.` );

	}

}

/** Validates one audited example's one-RenderSet-per-Scene policy and pass bindings. */
function lintAuditedExample( example, groupedSceneRoots, failures ) {

	const derivedReasons = deriveRenderSetReasons( example );
	const declaredReasons = new Set( example.renderSetReasons );
	for ( const reason of derivedReasons ) {

		if ( ! declaredReasons.has( reason ) ) addFailure( failures, example, `Missing derived renderSetReason '${ reason }'.` );

	}

	const complexSceneRoots = example.sceneRoots.filter( function hasRenderSet( sceneRoot ) { return sceneRoot.renderSetRuntimeInstanceCount === 1; } );
	const requiresRenderSet = complexSceneRoots.length > 0;
	if ( derivedReasons.length > 0 && ! requiresRenderSet ) addFailure( failures, example, 'Declared complexity requires at least one Scene root RenderSet.' );
	if ( requiresRenderSet && example.renderSetPolicy !== 'required' ) addFailure( failures, example, 'Complex Scene must use renderSetPolicy=required.' );
	if ( ! requiresRenderSet && example.renderSetPolicy !== 'not-required' ) addFailure( failures, example, 'Simple Scene must use renderSetPolicy=not-required.' );

	if ( requiresRenderSet && ( typeof example.renderSetType !== 'string' || example.renderSetType.length === 0 ) ) addFailure( failures, example, 'Required RenderSet example must declare renderSetType.' );
	if ( ! requiresRenderSet && example.renderSetType !== null ) addFailure( failures, example, 'Simple example must not declare renderSetType.' );

	const sceneRootNames = new Set();
	const sceneRootsByName = new Map();
	for ( const sceneRoot of example.sceneRoots ) {

		if ( sceneRootNames.has( sceneRoot.name ) ) addFailure( failures, example, `Duplicate Scene root '${ sceneRoot.name }'.` );
		sceneRootNames.add( sceneRoot.name );
		sceneRootsByName.set( sceneRoot.name, sceneRoot );
		if ( sceneRoot.renderSetRuntimeInstanceCount !== 0 && sceneRoot.renderSetRuntimeInstanceCount !== 1 ) addFailure( failures, example, `Scene root '${ sceneRoot.name }' must own zero or one RenderSet runtime instance.` );
		if ( sceneRoot.renderSetRuntimeInstanceCount === 1 && sceneRoot.renderSetType !== example.renderSetType ) addFailure( failures, example, `Complex Scene root '${ sceneRoot.name }' must use the declared renderSetType.` );
		if ( sceneRoot.renderSetRuntimeInstanceCount === 0 && sceneRoot.renderSetType !== null ) addFailure( failures, example, `Simple Scene root '${ sceneRoot.name }' must not declare a RenderSet type.` );

	}
	lintGeometryGroupSceneRoots( example, groupedSceneRoots, sceneRootsByName, failures );

	for ( const scenePass of example.scenePasses ) {

		if ( ! sceneRootNames.has( scenePass.sceneRoot ) ) addFailure( failures, example, `Scene pass '${ scenePass.name }' references an unknown Scene root.` );
		if ( typeof scenePass.renderClass !== 'string' || ! /^[A-Za-z_]\w*$/.test( scenePass.renderClass ) ) addFailure( failures, example, `Scene pass '${ scenePass.name }' must declare a valid generated renderClass.` );
		const sceneRoot = sceneRootsByName.get( scenePass.sceneRoot );
		if ( sceneRoot?.renderSetRuntimeInstanceCount === 1 ) {

			if ( scenePass.renderSetBindingCount !== 1 ) addFailure( failures, example, `Complex Scene pass '${ scenePass.name }' must bind exactly one RenderSet.` );
			if ( scenePass.usesStandaloneGeometry ) addFailure( failures, example, `Complex Scene pass '${ scenePass.name }' must not bind standalone geometry.` );
			if ( scenePass.usesExplicitDrawCount ) addFailure( failures, example, `Complex Scene pass '${ scenePass.name }' must use RenderSet-only draw.` );

		} else if ( sceneRoot ) {

			if ( scenePass.renderSetBindingCount !== 0 ) addFailure( failures, example, `Simple Scene pass '${ scenePass.name }' must not bind a RenderSet.` );

		}

	}

	if ( ! requiresRenderSet ) return;
	const roles = new Set( example.componentSchema.map( function mapRole( component ) { return component.role; } ) );
	for ( const role of REQUIRED_COMPONENT_ROLES ) {

		if ( ! roles.has( role ) ) addFailure( failures, example, `RenderSet componentSchema is missing '${ role }'.` );

	}

	if ( example.containsInstancing && ! example.capabilityAudit.evidence.some( function hasInstanceBuiltin( evidence ) { return evidence.includes( 'RenderEntityInstanceID' ); } ) ) {

		addFailure( failures, example, 'Instancing audit evidence must include RenderEntityInstanceID.' );

	}

}

/** Converts a correction proposal's root-scoped evidence records into the lint lookup map. */
export function readGeometryGroupSceneRootEvidence( evidenceDocument ) {

	const evidenceRecords = evidenceDocument.geometryGroupSceneRootEvidence;
	if ( ! Array.isArray( evidenceRecords ) ) throw new Error( 'Geometry-group evidence document must contain geometryGroupSceneRootEvidence.' );
	const evidenceByExample = new Map();
	for ( const [ evidenceIndex, evidence ] of evidenceRecords.entries() ) {

		if ( evidence === null || typeof evidence !== 'object' || Array.isArray( evidence ) ) throw new Error( `geometryGroupSceneRootEvidence[${ evidenceIndex }] must be an object.` );
		if ( typeof evidence.id !== 'string' || evidence.id.length === 0 ) throw new Error( `geometryGroupSceneRootEvidence[${ evidenceIndex }].id must be a non-empty string.` );
		if ( evidenceByExample.has( evidence.id ) ) throw new Error( `Geometry-group evidence repeats ${ evidence.id }.` );
		if ( ! Array.isArray( evidence.sceneRoots ) || evidence.sceneRoots.length === 0 ) throw new Error( `Geometry-group evidence for ${ evidence.id } must name at least one Scene root.` );
		evidenceByExample.set( evidence.id, [ ...evidence.sceneRoots ] );

	}
	return evidenceByExample;

}

/** Lazily loads the canonical root-scoped geometry-group evidence for standard lint calls. */
function getDefaultGeometryGroupSceneRootEvidence() {

	if ( defaultGeometryGroupSceneRootsByExample === null ) {

		defaultGeometryGroupSceneRootsByExample = readGeometryGroupSceneRootEvidence( readJson( defaultGeometryGroupEvidencePath ) );

	}
	return defaultGeometryGroupSceneRootsByExample;

}

/** Lints every audited example and optionally applies independent root-scoped group evidence. */
export function lintRenderSetPolicy( manifest, rejectPending = false, options = {} ) {

	const failures = [];
	let pendingCount = 0;
	const groupedSceneRootsByExample = options.geometryGroupSceneRootsByExample ?? getDefaultGeometryGroupSceneRootEvidence();
	if ( ! ( groupedSceneRootsByExample instanceof Map ) ) throw new Error( 'geometryGroupSceneRootsByExample must be a Map.' );
	const auditedExampleIds = new Set();

	for ( const example of manifest.examples ) {

		if ( example.status === 'excluded_upstream' ) continue;
		auditedExampleIds.add( example.id );
		if ( example.status === 'audit_pending' ) {

			pendingCount ++;
			if ( rejectPending ) addFailure( failures, example, 'Capability and RenderSet policy audit is still pending.' );
			continue;

		}

		lintAuditedExample( example, groupedSceneRootsByExample.get( example.id ) ?? [], failures );

	}
	for ( const exampleId of groupedSceneRootsByExample.keys() ) {

		if ( ! auditedExampleIds.has( exampleId ) ) failures.push( `${ exampleId }: Root-scoped geometry-group evidence does not match an audited manifest example.` );

	}
	return { failures, pendingCount };

}

/** Executes RenderSet policy lint with optional strict pending-audit rejection. */
function main() {

	const argumentsList = process.argv.slice( 2 );
	const manifestPath = readOption( argumentsList, '--manifest', defaultManifestPath );
	const geometryGroupEvidencePath = readOption( argumentsList, '--geometry-group-evidence', defaultGeometryGroupEvidencePath );
	const rejectPending = argumentsList.includes( '--strict' );
	const manifest = readJson( manifestPath );
	const geometryGroupSceneRootsByExample = readGeometryGroupSceneRootEvidence( readJson( geometryGroupEvidencePath ) );
	const { failures, pendingCount } = lintRenderSetPolicy( manifest, rejectPending, { geometryGroupSceneRootsByExample } );

	if ( failures.length > 0 ) {

		for ( const failure of failures ) console.error( `ERROR: ${ failure }` );
		process.exitCode = 1;
		return;

	}

	console.log( `RenderSet policy lint passed: pending=${ pendingCount }, strict=${ rejectPending }.` );

}

if ( process.argv[ 1 ] && path.resolve( process.argv[ 1 ] ) === fileURLToPath( import.meta.url ) ) main();
