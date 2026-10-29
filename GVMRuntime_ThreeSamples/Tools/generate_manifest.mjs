#!/usr/bin/env node

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import {
	THREE_R185_COMMIT,
	THREE_R185_RELEASE,
	buildExclusionMap,
	countStatuses,
	defaultExclusionsPath,
	defaultInventoryPath,
	defaultManifestPath,
	readJson,
	readOption,
	serializeJson
} from './manifest_common.mjs';
import { buildPhase1ManifestLock } from './phase1_manifest_lock_common.mjs';

const toolsDirectory = path.dirname( fileURLToPath( import.meta.url ) );
const manifestDirectory = path.resolve( toolsDirectory, '..', 'Manifest' );
const defaultAuditPath = path.join( manifestDirectory, 'three-r185-capability-audit.json' );
const defaultAdjudicationPath = path.join( manifestDirectory, 'three-r185-capability-adjudication.json' );
const defaultStatusLockPath = path.join( manifestDirectory, 'three-r185-phase1-status-lock.json' );

/** Creates the initial capability audit record for an upstream-excluded example. */
function createExcludedCapabilityAudit() {

	return {
		state: 'not_applicable',
		gpuWorkDslOnly: null,
		requiresNewPublicCapability: null,
		requiredCapabilities: [],
		availableCapabilities: [],
		missingCapabilities: [],
		evidence: [],
		notes: [ 'Excluded before the phase-1 capability audit.' ]
	};

}

/** Creates an explicit, incomplete capability audit record for a phase-1 candidate. */
function createPendingCapabilityAudit() {

	return {
		state: 'pending',
		gpuWorkDslOnly: true,
		requiresNewPublicCapability: null,
		requiredCapabilities: [],
		availableCapabilities: [],
		missingCapabilities: [],
		evidence: [],
		notes: []
	};

}

/** Creates one deterministic manifest entry from the pinned upstream inventory. */
function createExample( category, exampleId, exclusion ) {

	const isExcluded = exclusion !== undefined;
	return {
		id: exampleId,
		category,
		upstreamPath: `examples/${ exampleId }.html`,
		status: isExcluded ? 'excluded_upstream' : 'audit_pending',
		exclusion: isExcluded ? exclusion : null,
		capabilityAudit: isExcluded ? createExcludedCapabilityAudit() : createPendingCapabilityAudit(),
		renderSetPolicy: null,
		renderSetReasons: [],
		sceneRoots: [],
		renderableObjectCount: null,
		containsInstancing: null,
		containsHierarchy: null,
		containsLod: null,
		containsDynamicObjects: null,
		containsMultipleMaterials: null,
		containsGeometryGroups: null,
		loaderRenderableObjectCount: null,
		scenePasses: [],
		screenPasses: [],
		renderSetType: null,
		componentSchema: [],
		dslShard: null,
		scenarios: [],
		deferredEvidence: null
	};

}

/** Builds the complete r185 manifest from pinned inventory and exclusion sources. */
export function buildManifest( inventory, exclusionSource ) {

	const exclusionMap = buildExclusionMap( exclusionSource );
	const examples = [];
	for ( const [ category, exampleIds ] of Object.entries( inventory ) ) {

		for ( const exampleId of exampleIds ) {

			examples.push( createExample( category, exampleId, exclusionMap.get( exampleId ) ) );

		}

	}

	return {
		schemaVersion: 1,
		upstream: {
			release: THREE_R185_RELEASE,
			commit: THREE_R185_COMMIT,
			inventorySource: exclusionSource.upstreamEvidence.inventory,
			officialExceptionSource: exclusionSource.upstreamEvidence.officialExceptionList
		},
		samplePolicy: {
			mode: 'single-sample',
			upstreamMsaaDoesNotExcludeExample: true,
			msaaEnabled: false,
			simulateMsaa: false,
			requireMsaaParity: false,
			description: 'Phase 1 renders every retained example without MSAA or MSAA simulation; upstream MSAA usage does not exclude an otherwise supported example.'
		},
		counts: countStatuses( examples ),
		examples
	};

}

/** Removes obsolete multisample-simulation declarations from the generated contract. */
export function applySingleSamplePolicy( manifest ) {

	const isObsoleteDeclaration = function isObsoleteDeclaration( value ) {

		return typeof value === 'string' &&
			/msaa|supersampl|four[-_ ]sample|deterministic[-_ ]single[-_ ]sample[-_ ]downsample|canvas[-_ ]resolve|coverage[-_ ]resolve/iu.test( value );

	};
	for ( const example of manifest.examples ) {

		for ( const fieldName of [ 'requiredCapabilities', 'availableCapabilities', 'missingCapabilities' ] ) {

			const declarations = example.capabilityAudit?.[ fieldName ];
			if ( Array.isArray( declarations ) ) example.capabilityAudit[ fieldName ] = declarations.filter( function keepSingleSampleDeclaration( declaration ) {

				return ! isObsoleteDeclaration( declaration );

			} );

		}
		if ( Array.isArray( example.screenPasses ) ) example.screenPasses = example.screenPasses.filter( function keepSingleSamplePass( passName ) {

			return ! isObsoleteDeclaration( passName );

		} );

	}
	return manifest;

}

/** Executes generation or deterministic check mode using command-line arguments only. */
function main() {

	const argumentsList = process.argv.slice( 2 );
	const inventoryPath = readOption( argumentsList, '--inventory', defaultInventoryPath );
	const exclusionsPath = readOption( argumentsList, '--exclusions', defaultExclusionsPath );
	const outputPath = readOption( argumentsList, '--output', defaultManifestPath );
	const baselineManifest = buildManifest( readJson( inventoryPath ), readJson( exclusionsPath ) );
	let generatedManifest = baselineManifest;
	if ( ! argumentsList.includes( '--baseline-only' ) ) {

		const auditPath = readOption( argumentsList, '--audit', defaultAuditPath );
		const adjudicationPath = readOption( argumentsList, '--adjudication', defaultAdjudicationPath );
		const statusLockPath = readOption( argumentsList, '--status-lock', defaultStatusLockPath );
		generatedManifest = buildPhase1ManifestLock(
			baselineManifest,
			readJson( auditPath ),
			readJson( adjudicationPath ),
			readJson( statusLockPath )
		).manifest;

	}
	generatedManifest = applySingleSamplePolicy( generatedManifest );
	const generatedText = serializeJson( generatedManifest );

	if ( argumentsList.includes( '--check' ) ) {

		const currentText = fs.readFileSync( outputPath, 'utf8' );
		if ( currentText !== generatedText ) {

			throw new Error( `${ outputPath } is stale. Regenerate it with generate_manifest.mjs.` );

		}

		console.log( `Manifest is reproducible: ${ outputPath }` );
		return;

	}

	fs.mkdirSync( path.dirname( outputPath ), { recursive: true } );
	fs.writeFileSync( outputPath, generatedText );
	console.log( `Generated ${ outputPath }` );

}

if ( process.argv[ 1 ] && path.resolve( process.argv[ 1 ] ) === fileURLToPath( import.meta.url ) ) main();
