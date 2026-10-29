#!/usr/bin/env node

import childProcess from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import {
	EXPECTED_AUDIT_POPULATION,
	THREE_R185_COMMIT,
	readJson
} from './manifest_common.mjs';
import {
	buildCapabilityAudit,
	createCapabilityAuditInputManifest,
	serializeCapabilityAudit,
	validateCapabilityAudit,
	writeOrCheckCapabilityAudit
} from './three_capability_audit_common.mjs';

const toolsDirectory = path.dirname( fileURLToPath( import.meta.url ) );
const defaultCapabilityFreezePath = path.resolve( toolsDirectory, '..', 'Manifest', 'phase1-capability-freeze.json' );
const valueOptions = new Set( [ '--upstream-root', '--manifest', '--output' ] );
const flagOptions = new Set( [ '--check' ] );

/** Rejects unknown, repeated, or value-less options before reading audit inputs. */
function validateArguments( argumentsList ) {

	const seenOptions = new Set();
	for ( let argumentIndex = 0; argumentIndex < argumentsList.length; argumentIndex ++ ) {

		const argument = argumentsList[ argumentIndex ];
		if ( ! valueOptions.has( argument ) && ! flagOptions.has( argument ) ) throw new Error( `Unknown option '${ argument }'.` );
		if ( seenOptions.has( argument ) ) throw new Error( `Option '${ argument }' was provided more than once.` );
		seenOptions.add( argument );
		if ( valueOptions.has( argument ) ) {

			if ( argumentIndex + 1 >= argumentsList.length || argumentsList[ argumentIndex + 1 ].startsWith( '--' ) ) throw new Error( `Required value for ${ argument } is missing.` );
			argumentIndex ++;

		}

	}

}

/** Reads one required value-bearing command-line option as an absolute path. */
function readRequiredPathOption( argumentsList, optionName ) {

	const optionIndex = argumentsList.indexOf( optionName );
	if ( optionIndex === - 1 || optionIndex + 1 >= argumentsList.length ) throw new Error( `Required option ${ optionName } is missing.` );
	return path.resolve( argumentsList[ optionIndex + 1 ] );

}

/** Verifies that the audit input is the exact pinned Three.js r185 checkout. */
function verifyUpstreamCommit( upstreamRoot ) {

	const result = childProcess.spawnSync( 'git', [ '-C', upstreamRoot, 'rev-parse', 'HEAD' ], { encoding: 'utf8' } );
	if ( result.status !== 0 ) throw new Error( `Unable to read upstream revision at ${ upstreamRoot }: ${ result.stderr.trim() }` );
	const commit = result.stdout.trim();
	if ( commit !== THREE_R185_COMMIT ) throw new Error( `Upstream revision is ${ commit }; expected ${ THREE_R185_COMMIT }.` );

}

/** Executes deterministic audit generation or stale-output checking. */
function main() {

	const argumentsList = process.argv.slice( 2 );
	validateArguments( argumentsList );
	const upstreamRoot = readRequiredPathOption( argumentsList, '--upstream-root' );
	const manifestPath = readRequiredPathOption( argumentsList, '--manifest' );
	const outputPath = readRequiredPathOption( argumentsList, '--output' );
	verifyUpstreamCommit( upstreamRoot );
	const manifestText = fs.readFileSync( manifestPath, 'utf8' );
	const capabilityFreezeText = fs.readFileSync( defaultCapabilityFreezePath, 'utf8' );
	const sourceManifest = JSON.parse( manifestText );
	const capabilityFreeze = JSON.parse( capabilityFreezeText );
	if ( sourceManifest.upstream?.commit !== THREE_R185_COMMIT ) throw new Error( `Manifest upstream revision must be ${ THREE_R185_COMMIT }.` );
	const auditManifest = createCapabilityAuditInputManifest( sourceManifest );
	const auditManifestText = `${ JSON.stringify( auditManifest, null, 2 ) }\n`;
	const audit = buildCapabilityAudit( upstreamRoot, auditManifest, capabilityFreeze, auditManifestText, capabilityFreezeText );
	const failures = validateCapabilityAudit( audit, auditManifest, EXPECTED_AUDIT_POPULATION, upstreamRoot );
	if ( failures.length > 0 ) throw new Error( `Capability audit validation failed:\n${ failures.map( function formatFailure( failure ) { return `- ${ failure }`; } ).join( '\n' ) }` );
	const serializedAudit = serializeCapabilityAudit( audit );
	if ( argumentsList.includes( '--check' ) ) {

		writeOrCheckCapabilityAudit( outputPath, serializedAudit, true );
		console.log( `Three r185 capability audit is reproducible: ${ outputPath }` );
		return;

	}
	writeOrCheckCapabilityAudit( outputPath, serializedAudit, false );
	console.log( `Generated ${ outputPath }: audited=${ audit.counts.auditedExamples }, missing-capability-candidates=${ audit.counts.examplesWithMissingCapabilityCandidates }, renderset-trigger-candidates=${ audit.counts.examplesWithRenderSetTriggerCandidates }` );

}

main();
