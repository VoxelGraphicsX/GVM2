#!/usr/bin/env node

import path from 'node:path';
import { fileURLToPath } from 'node:url';

import {
	EXPECTED_AUDIT_POPULATION,
	EXPECTED_EXCLUDED,
	EXPECTED_TOTAL,
	THREE_R185_COMMIT,
	VALID_STATUSES,
	buildExclusionMap,
	countStatuses,
	defaultExclusionsPath,
	defaultManifestPath,
	formatCounts,
	readJson,
	readOption
} from './manifest_common.mjs';

/** Adds a validation failure with the affected example identifier when available. */
function addFailure( failures, example, message ) {

	failures.push( `${ example ? `${ example.id }: ` : '' }${ message }` );

}

/** Validates fields that every manifest example must expose to runners and linters. */
function validateRequiredFields( example, failures ) {

	const requiredFields = [
		'id', 'category', 'upstreamPath', 'status', 'exclusion', 'capabilityAudit',
		'renderSetPolicy', 'renderSetReasons', 'sceneRoots', 'renderableObjectCount',
		'containsInstancing', 'containsHierarchy', 'containsLod', 'containsDynamicObjects',
		'containsMultipleMaterials', 'containsGeometryGroups', 'loaderRenderableObjectCount',
		'scenePasses', 'screenPasses', 'renderSetType', 'componentSchema', 'dslShard',
		'scenarios', 'deferredEvidence'
	];

	for ( const fieldName of requiredFields ) {

		if ( ! Object.hasOwn( example, fieldName ) ) addFailure( failures, example, `Missing field '${ fieldName }'.` );

	}

}

/** Rejects per-example MSAA or supersampling implementation declarations under the global single-sample policy. */
function validateSingleSampleContract( example, failures ) {

	const declarations = [
		...( example.capabilityAudit?.requiredCapabilities ?? [] ),
		...( example.capabilityAudit?.availableCapabilities ?? [] ),
		...( example.capabilityAudit?.missingCapabilities ?? [] ),
		...( example.screenPasses ?? [] )
	];
	for ( const declaration of declarations ) {

		if ( /msaa|supersampl|four[-_ ]sample|deterministic[-_ ]single[-_ ]sample[-_ ]downsample|canvas[-_ ]resolve|coverage[-_ ]resolve/iu.test( declaration ) ) {

			addFailure( failures, example, `Single-sample policy forbids MSAA or supersampling declaration '${ declaration }'.` );

		}

	}

}

/** Validates status-specific audit and evidence invariants for one example. */
function validateStatusContract( example, expectedExclusion, failures ) {

	if ( ! VALID_STATUSES.has( example.status ) ) {

		addFailure( failures, example, `Invalid status '${ example.status }'.` );
		return;

	}

	if ( example.status === 'excluded_upstream' ) {

		if ( expectedExclusion === undefined ) addFailure( failures, example, 'Unexpected upstream exclusion.' );
		if ( example.exclusion === null ) addFailure( failures, example, 'Excluded example must contain exclusion evidence.' );
		if ( example.capabilityAudit?.state !== 'not_applicable' ) addFailure( failures, example, 'Excluded example audit state must be not_applicable.' );
		return;

	}

	if ( expectedExclusion !== undefined ) addFailure( failures, example, 'Pinned upstream exclusion must use excluded_upstream status.' );
	if ( example.exclusion !== null ) addFailure( failures, example, 'Non-excluded example must not contain exclusion evidence.' );
	if ( example.status === 'phase1_required' || example.status === 'deferred_missing_capability' ) {

		const lockedComplexityFields = [
			'renderableObjectCount', 'containsInstancing', 'containsHierarchy', 'containsLod',
			'containsDynamicObjects', 'containsMultipleMaterials', 'containsGeometryGroups',
			'loaderRenderableObjectCount'
		];
		for ( const fieldName of lockedComplexityFields ) {

			if ( example[ fieldName ] === null || example[ fieldName ] === undefined ) addFailure( failures, example, `Locked status must adjudicate '${ fieldName }'.` );

		}
		if ( example.capabilityAudit?.gpuWorkDslOnly !== true ) addFailure( failures, example, 'Locked status must require gpuWorkDslOnly=true.' );
		if ( example.renderSetPolicy !== 'required' && example.renderSetPolicy !== 'not-required' ) addFailure( failures, example, 'Locked status must adjudicate renderSetPolicy.' );

	}

	if ( example.status === 'audit_pending' ) {

		if ( example.capabilityAudit?.state !== 'pending' ) addFailure( failures, example, 'Pending example audit state must be pending.' );
		if ( example.deferredEvidence !== null ) addFailure( failures, example, 'Pending example must not contain deferred evidence.' );

	} else if ( example.status === 'phase1_required' ) {

		if ( example.capabilityAudit?.state !== 'supported' ) addFailure( failures, example, 'Required example audit state must be supported.' );
		if ( example.capabilityAudit?.requiresNewPublicCapability !== false ) addFailure( failures, example, 'Required example must explicitly require no new public capability.' );
		if ( example.deferredEvidence !== null ) addFailure( failures, example, 'Required example must not contain deferred evidence.' );
		if ( ! Array.isArray( example.scenarios ) || example.scenarios.length === 0 ) addFailure( failures, example, 'Required example must lock deterministic scenarios.' );
		if ( ! example.scenarios.some( function isInitialScenario( scenario ) { return scenario.kind === 'initial-frame'; } ) ) addFailure( failures, example, 'Required example must include an initial-frame scenario.' );
		const scenarioIds = new Set();
		const scenePassKeys = new Set( example.scenePasses.map( function makeScenePassKey( scenePass ) {

			return `${ scenePass.sceneRoot }::${ scenePass.name }`;

		} ) );
		for ( const scenario of example.scenarios ) {

			if ( scenarioIds.has( scenario.id ) ) addFailure( failures, example, `Duplicate scenario id '${ scenario.id }'.` );
			scenarioIds.add( scenario.id );
			if ( scenario.kind === 'input-replay' && ( typeof scenario.inputReplay !== 'string' || scenario.inputReplay.length === 0 ) ) addFailure( failures, example, `Input replay scenario '${ scenario.id }' must declare inputReplay.` );
			if ( scenario.renderableObjectCount !== undefined
				&& scenario.renderableObjectCount !== example.renderableObjectCount
				&& example.containsDynamicObjects !== true ) {

				addFailure( failures, example, `Scenario '${ scenario.id }' changes renderableObjectCount without dynamic objects.` );

			}
			const invocationKeys = new Set();
			const invocationCountByPass = new Map();
			for ( const invocation of scenario.scenePassInvocations ?? [] ) {

				const invocationKey = `${ invocation.sceneRoot }::${ invocation.scenePass }`;
				if ( invocationKeys.has( invocationKey ) ) addFailure( failures, example, `Scenario '${ scenario.id }' duplicates Scene pass invocation '${ invocationKey }'.` );
				invocationKeys.add( invocationKey );
				invocationCountByPass.set( invocationKey, invocation.invocationCount );
				if ( ! scenePassKeys.has( invocationKey ) ) addFailure( failures, example, `Scenario '${ scenario.id }' references unknown Scene pass '${ invocationKey }'.` );

			}
			if ( scenario.scenePassSequence !== undefined ) {

				const sequenceCountByPass = new Map();
				for ( const sequenceEntry of scenario.scenePassSequence ) {

					const sequenceKey = `${ sequenceEntry.sceneRoot }::${ sequenceEntry.scenePass }`;
					if ( ! scenePassKeys.has( sequenceKey ) ) addFailure( failures, example, `Scenario '${ scenario.id }' sequence references unknown Scene pass '${ sequenceKey }'.` );
					sequenceCountByPass.set( sequenceKey, ( sequenceCountByPass.get( sequenceKey ) ?? 0 ) + 1 );

				}
				for ( const scenePassKey of scenePassKeys ) {

					const expectedInvocationCount = invocationCountByPass.get( scenePassKey ) ?? 1;
					const sequencedInvocationCount = sequenceCountByPass.get( scenePassKey ) ?? 0;
					if ( sequencedInvocationCount !== expectedInvocationCount ) addFailure( failures, example, `Scenario '${ scenario.id }' sequence contains ${ sequencedInvocationCount} invocation(s) of '${ scenePassKey }', expected ${ expectedInvocationCount }.` );

				}

			}

		}
		if ( example.id.includes( '_loader_' ) && ! example.scenarios.some( function isLoaderSnapshot( scenario ) { return scenario.kind === 'loader-snapshot'; } ) ) {

			addFailure( failures, example, 'Loader example must include a canonical loader-snapshot scenario.' );

		}
		if ( example.id.includes( '_exporter_' ) && ! example.scenarios.some( function isExporterRoundTrip( scenario ) { return scenario.kind === 'export-round-trip'; } ) ) {

			addFailure( failures, example, 'Exporter example must include a canonical export-round-trip scenario.' );

		}

	} else if ( example.status === 'deferred_missing_capability' ) {

		if ( example.capabilityAudit?.state !== 'deferred' ) addFailure( failures, example, 'Deferred example audit state must be deferred.' );
		if ( example.capabilityAudit?.requiresNewPublicCapability !== true ) addFailure( failures, example, 'Deferred example must explicitly require a new public capability.' );
		validateDeferredEvidence( example, failures );

	}
	if ( example.status === 'phase1_required' || example.status === 'deferred_missing_capability' ) {

		const scenePassKeys = new Set();
		for ( const scenePass of example.scenePasses ) {

			const scenePassKey = `${ scenePass.sceneRoot }::${ scenePass.name }`;
			if ( scenePassKeys.has( scenePassKey ) ) addFailure( failures, example, `Duplicate Scene pass '${ scenePassKey }'.` );
			scenePassKeys.add( scenePassKey );

		}

	}

}

/** Validates the complete evidence record required for every deferred example. */
function validateDeferredEvidence( example, failures ) {

	const evidence = example.deferredEvidence;
	const requiredFields = [
		'reasonCode', 'upstreamSourceEvidence', 'missingCapability',
		'whyCurrentDslCannotExpressIt', 'minimumFutureApi', 'reentryTest'
	];

	if ( evidence === null || typeof evidence !== 'object' ) {

		addFailure( failures, example, 'Deferred example must include deferredEvidence.' );
		return;

	}

	for ( const fieldName of requiredFields ) {

		const value = evidence[ fieldName ];
		if ( value === undefined || value === '' || Array.isArray( value ) && value.length === 0 ) {

			addFailure( failures, example, `Deferred evidence '${ fieldName }' must be populated.` );

		}

	}

}

/** Validates the complete manifest against pinned counts and source exclusions. */
export function validateManifest( manifest, exclusionSource ) {

	const failures = [];
	if ( manifest.schemaVersion !== 1 ) addFailure( failures, null, 'schemaVersion must be 1.' );
	if ( manifest.upstream?.commit !== THREE_R185_COMMIT ) addFailure( failures, null, `Upstream commit must be ${ THREE_R185_COMMIT }.` );
	if ( manifest.samplePolicy?.mode !== 'single-sample'
		|| manifest.samplePolicy?.upstreamMsaaDoesNotExcludeExample !== true
		|| manifest.samplePolicy?.msaaEnabled !== false
		|| manifest.samplePolicy?.simulateMsaa !== false
		|| manifest.samplePolicy?.requireMsaaParity !== false ) {

		addFailure( failures, null, 'samplePolicy must keep MSAA examples in scope while requiring normal single-sample rendering without simulation or parity claims.' );

	}
	if ( ! Array.isArray( manifest.examples ) ) return { failures: [ 'examples must be an array.' ], counts: null };

	const expectedExclusions = buildExclusionMap( exclusionSource );
	const seenIds = new Set();
	const renderClassOwners = new Map();
	for ( const example of manifest.examples ) {

		validateRequiredFields( example, failures );
		if ( seenIds.has( example.id ) ) addFailure( failures, example, 'Duplicate example identifier.' );
		seenIds.add( example.id );
		if ( example.upstreamPath !== `examples/${ example.id }.html` ) addFailure( failures, example, 'upstreamPath does not match the example identifier.' );
		validateSingleSampleContract( example, failures );
		validateStatusContract( example, expectedExclusions.get( example.id ), failures );
		if ( example.status === 'phase1_required' || example.status === 'deferred_missing_capability' ) {

			for ( const scenePass of example.scenePasses ) {

				const existingOwner = renderClassOwners.get( scenePass.renderClass );
				if ( existingOwner !== undefined ) addFailure( failures, example, `Generated RenderClass '${ scenePass.renderClass }' is already owned by ${ existingOwner}.` );
				else renderClassOwners.set( scenePass.renderClass, example.id );

			}

		}

	}

	const counts = countStatuses( manifest.examples );
	if ( counts.total !== EXPECTED_TOTAL ) addFailure( failures, null, `Expected ${ EXPECTED_TOTAL } examples, received ${ counts.total }.` );
	if ( counts.excludedUpstream !== EXPECTED_EXCLUDED ) addFailure( failures, null, `Expected ${ EXPECTED_EXCLUDED } exclusions, received ${ counts.excludedUpstream }.` );
	if ( counts.auditPopulation !== EXPECTED_AUDIT_POPULATION ) addFailure( failures, null, `Expected ${ EXPECTED_AUDIT_POPULATION } audit candidates, received ${ counts.auditPopulation }.` );
	if ( counts.total !== counts.excludedUpstream + counts.auditPopulation ) addFailure( failures, null, '588 = excluded + audit population invariant failed.' );
	if ( counts.auditPopulation !== counts.auditPending + counts.phase1Required + counts.deferredMissingCapability ) addFailure( failures, null, '511 = pending + required + deferred invariant failed.' );
	if ( JSON.stringify( counts ) !== JSON.stringify( manifest.counts ) ) addFailure( failures, null, 'Stored counts are stale.' );
	if ( expectedExclusions.size !== EXPECTED_EXCLUDED ) addFailure( failures, null, `Exclusion source resolves to ${ expectedExclusions.size }, expected ${ EXPECTED_EXCLUDED }.` );

	return { failures, counts };

}

/** Executes manifest validation using explicit command-line path overrides. */
function main() {

	const argumentsList = process.argv.slice( 2 );
	const manifestPath = readOption( argumentsList, '--manifest', defaultManifestPath );
	const exclusionsPath = readOption( argumentsList, '--exclusions', defaultExclusionsPath );
	const result = validateManifest( readJson( manifestPath ), readJson( exclusionsPath ) );
	if ( result.failures.length > 0 ) {

		for ( const failure of result.failures ) console.error( `ERROR: ${ failure }` );
		process.exitCode = 1;
		return;

	}

	console.log( `Manifest valid: ${ formatCounts( result.counts ) }` );

}

if ( process.argv[ 1 ] && path.resolve( process.argv[ 1 ] ) === fileURLToPath( import.meta.url ) ) main();
