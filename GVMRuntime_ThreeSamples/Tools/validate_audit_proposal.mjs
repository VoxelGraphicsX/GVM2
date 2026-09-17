#!/usr/bin/env node

import crypto from 'node:crypto';
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { applySingleSamplePolicy, buildManifest } from './generate_manifest.mjs';
import { lintRenderSetPolicy } from './lint_render_set_policy.mjs';
import {
	THREE_R185_COMMIT,
	defaultExclusionsPath,
	defaultInventoryPath,
	readJson
} from './manifest_common.mjs';
import { buildPhase1ManifestLock } from './phase1_manifest_lock_common.mjs';
import { validateStatusLock } from './lock_phase1_manifest.mjs';
import { validateJsonSchema } from './validate_manifest_schema.mjs';
import { validateManifest } from './validate_manifest.mjs';

const toolsDirectory = path.dirname( fileURLToPath( import.meta.url ) );
const repositoryRoot = path.resolve( toolsDirectory, '..', '..' );
const manifestDirectory = path.resolve( toolsDirectory, '..', 'Manifest' );
const defaultAuditPath = path.join( manifestDirectory, 'three-r185-capability-audit.json' );
const defaultAdjudicationPath = path.join( manifestDirectory, 'three-r185-capability-adjudication.json' );
const defaultStatusLockPath = path.join( manifestDirectory, 'three-r185-phase1-status-lock.json' );
const defaultSchemaPath = path.join( manifestDirectory, 'three-r185-manifest.schema.json' );
const proposalKind = 'three-r185-phase1-capability-and-render-set-review';
const supportedStatuses = new Set( [ 'phase1_required', 'deferred_missing_capability' ] );
const renderFieldNames = [
	'renderSetPolicy', 'renderSetReasons', 'sceneRoots', 'renderableObjectCount',
	'containsInstancing', 'containsHierarchy', 'containsLod', 'containsDynamicObjects',
	'containsMultipleMaterials', 'containsGeometryGroups', 'loaderRenderableObjectCount',
	'scenePasses', 'screenPasses', 'renderSetType', 'componentSchema'
];

/** Requires one non-empty string and returns its unchanged value. */
function requireText( value, label ) {

	if ( typeof value !== 'string' || value.trim().length === 0 ) throw new Error( `${ label } must be a non-empty string.` );
	return value;

}

/** Requires one array and returns its unchanged value. */
function requireArray( value, label ) {

	if ( ! Array.isArray( value ) ) throw new Error( `${ label } must be an array.` );
	return value;

}

/** Rejects absolute paths and traversal before joining one repository-relative path. */
function resolveRelativePath( root, relativePath, label ) {

	requireText( relativePath, label );
	if ( path.isAbsolute( relativePath ) || relativePath.split( /[\\/]/ ).includes( '..' ) ) throw new Error( `${ label } must be repository-relative without traversal.` );
	const resolvedRoot = path.resolve( root );
	const resolvedPath = path.resolve( resolvedRoot, relativePath );
	if ( resolvedPath !== resolvedRoot && ! resolvedPath.startsWith( `${ resolvedRoot }${ path.sep }` ) ) throw new Error( `${ label } escapes its root.` );
	return resolvedPath;

}

/** Computes the Git blob SHA-1 for raw canonical asset bytes. */
function computeGitBlobSha1( bytes ) {

	return crypto.createHash( 'sha1' ).update( `blob ${ bytes.length }\0` ).update( bytes ).digest( 'hex' );

}

/** Loads one canonical upstream asset from the checkout or its pinned Git object. */
function loadUpstreamAsset( upstreamRoot, sourcePath, label ) {

	const assetPath = resolveRelativePath( upstreamRoot, sourcePath, `${ label }.path` );
	if ( fs.existsSync( assetPath ) ) return fs.readFileSync( assetPath );
	try {

		const blob = execFileSync( 'git', [ '-C', upstreamRoot, 'rev-parse', `HEAD:${ sourcePath }` ], { encoding: 'utf8' } ).trim();
		return execFileSync( 'git', [ '-C', upstreamRoot, 'cat-file', 'blob', blob ], { maxBuffer: 512 * 1024 * 1024 } );

	} catch ( error ) {

		throw new Error( `${ label } does not exist in the pinned upstream checkout: ${ sourcePath } (${ error.message }).` );

	}

}

/** Converts proposal-local asset topology records into status-lock canonical evidence. */
function collectAssetEvidence( proposal, upstreamRoot, label ) {

	const evidenceByPath = new Map();
	for ( const evidence of proposal.assetEvidence ?? [] ) {

		requireText( evidence.sourcePath, `${ label }.assetEvidence.sourcePath` );
		if ( evidenceByPath.has( evidence.sourcePath ) ) throw new Error( `${ label } repeats asset evidence for ${ evidence.sourcePath }.` );
		evidenceByPath.set( evidence.sourcePath, structuredClone( evidence ) );

	}
	let externalAssetCount = 0;
	for ( const [ assetIndex, asset ] of ( proposal.canonicalAssetTopology?.assets ?? [] ).entries() ) {

		const assetLabel = `${ label }.canonicalAssetTopology.assets[${ assetIndex }]`;
		requireText( asset.sha256, `${ assetLabel }.sha256` );
		if ( ! /^[a-f0-9]{64}$/u.test( asset.sha256 ) ) throw new Error( `${ assetLabel }.sha256 must be lowercase SHA-256.` );
		if ( typeof asset.url === 'string' ) {

			requireText( asset.url, `${ assetLabel }.url` );
			externalAssetCount ++;
			continue;

		}
		const sourcePath = requireText( asset.path, `${ assetLabel }.path` );
		const bytes = loadUpstreamAsset( upstreamRoot, sourcePath, assetLabel );
		const sha256 = crypto.createHash( 'sha256' ).update( bytes ).digest( 'hex' );
		if ( sha256 !== asset.sha256 ) throw new Error( `${ assetLabel } SHA-256 does not match ${ sourcePath }.` );
		const existing = evidenceByPath.get( sourcePath );
		if ( existing && existing.sha256 !== sha256 ) throw new Error( `${ assetLabel } conflicts with assetEvidence for ${ sourcePath }.` );
		if ( ! existing ) {

			evidenceByPath.set( sourcePath, {
				sourcePath,
				gitBlobSha1: computeGitBlobSha1( bytes ),
				sha256,
				semantic: 'Canonical asset locked by the complete-entry capability review.'
			} );

		}

	}
	return { assetEvidence: [ ...evidenceByPath.values() ], externalAssetCount };

}

/** Copies status-lock fields shared by required and manually deferred reviews. */
function normalizeCommonReview( proposal, upstreamRoot, label ) {

	const normalized = {
		id: requireText( proposal.id, `${ label }.id` ),
		reviewScope: requireText( proposal.reviewScope, `${ label }.reviewScope` ),
		upstreamSourceEvidence: structuredClone( requireArray( proposal.upstreamSourceEvidence, `${ label }.upstreamSourceEvidence` ) ),
		repositoryCapabilityEvidence: structuredClone( requireArray( proposal.repositoryCapabilityEvidence, `${ label }.repositoryCapabilityEvidence` ) ),
		requiredCapabilities: structuredClone( requireArray( proposal.requiredCapabilities, `${ label }.requiredCapabilities` ) ),
		availableCapabilities: structuredClone( requireArray( proposal.availableCapabilities, `${ label }.availableCapabilities` ) ),
		notes: structuredClone( requireArray( proposal.notes, `${ label }.notes` ) )
	};
	for ( const fieldName of renderFieldNames ) {

		if ( ! Object.hasOwn( proposal, fieldName ) ) throw new Error( `${ label } is missing ${ fieldName }.` );
		normalized[ fieldName ] = structuredClone( proposal[ fieldName ] );

	}
	const assets = collectAssetEvidence( proposal, upstreamRoot, label );
	if ( assets.assetEvidence.length > 0 ) normalized.assetEvidence = assets.assetEvidence;
	if ( proposal.externalAssetRequirements !== undefined ) {

		const requirements = proposal.externalAssetRequirements;
		if ( requirements === null || typeof requirements !== 'object' || Array.isArray( requirements ) ) throw new Error( `${ label }.externalAssetRequirements must be an object.` );
		if ( requirements.schemaVersion !== 1 ) throw new Error( `${ label }.externalAssetRequirements.schemaVersion must be 1.` );
		if ( ! Array.isArray( requirements.mappings ) || requirements.mappings.length === 0 ) throw new Error( `${ label }.externalAssetRequirements.mappings must be a non-empty array.` );
		normalized.externalAssetRequirements = structuredClone( requirements );

	}
	return { normalized, externalAssetCount: assets.externalAssetCount };

}

/** Converts one proposal entry to a status-lock required or deferred review. */
function normalizeProposal( proposal, upstreamRoot, label ) {

	const status = requireText( proposal.proposedStatus, `${ label }.proposedStatus` );
	if ( ! supportedStatuses.has( status ) ) throw new Error( `${ label } has unsupported status '${ status }'.` );
	const { normalized, externalAssetCount } = normalizeCommonReview( proposal, upstreamRoot, label );
	if ( proposal.upstreamPath !== `examples/${ normalized.id }.html` ) throw new Error( `${ label }.upstreamPath must match examples/${ normalized.id }.html.` );
	if ( status === 'phase1_required' ) {

		if ( ( proposal.missingCapabilities ?? [] ).length > 0 ) throw new Error( `${ label } cannot be required with missing capabilities.` );
		if ( proposal.deferredEvidence !== null && proposal.deferredEvidence !== undefined ) throw new Error( `${ label } required review must not contain deferredEvidence.` );
		normalized.dslShard = requireText( proposal.dslShard, `${ label }.dslShard` );
		normalized.scenarios = structuredClone( requireArray( proposal.scenarios, `${ label }.scenarios` ) );
		return { status, review: normalized, externalAssetCount };

	}
	const deferredEvidence = proposal.deferredEvidence;
	if ( deferredEvidence === null || typeof deferredEvidence !== 'object' || Array.isArray( deferredEvidence ) ) throw new Error( `${ label } deferred review must contain deferredEvidence.` );
	normalized.missingCapabilities = structuredClone( requireArray( proposal.missingCapabilities, `${ label }.missingCapabilities` ) );
	normalized.reasonCode = requireText( deferredEvidence.reasonCode, `${ label }.deferredEvidence.reasonCode` );
	normalized.whyCurrentDslCannotExpressIt = requireText( deferredEvidence.whyCurrentDslCannotExpressIt, `${ label }.deferredEvidence.whyCurrentDslCannotExpressIt` );
	normalized.minimumFutureApi = requireText( deferredEvidence.minimumFutureApi, `${ label }.deferredEvidence.minimumFutureApi` );
	normalized.reentryTest = requireText( deferredEvidence.reentryTest, `${ label }.deferredEvidence.reentryTest` );
	return { status, review: normalized, externalAssetCount };

}

/** Validates a proposal by applying it to an in-memory lock and running every manifest gate. */
export function validateAuditProposal( inputs ) {

	const { proposal, upstreamRoot, inventory, exclusions, capabilityAudit, adjudication, statusLock, manifestSchema } = inputs;
	const replaceLocked = inputs.replaceLocked === true;
	if ( proposal.schemaVersion !== 1 ) throw new Error( 'Proposal schemaVersion must be 1.' );
	if ( proposal.proposalKind !== proposalKind ) throw new Error( `Proposal kind must be ${ proposalKind }.` );
	if ( proposal.upstreamCommit !== THREE_R185_COMMIT ) throw new Error( `Proposal upstream commit must be ${ THREE_R185_COMMIT }.` );
	const proposals = requireArray( proposal.proposals, 'proposal.proposals' );
	if ( proposals.length === 0 ) throw new Error( 'Proposal must contain at least one review.' );
	const manifestIds = Object.values( inventory ).flat().map( function mapInventoryId( entry ) {

		return typeof entry === 'string' ? entry : entry.id;

	} );
	const manifestOrder = new Map( manifestIds.map( function mapOrder( id, index ) { return [ id, index ]; } ) );
	const lockedStatuses = new Map( [
		...statusLock.phase1RequiredReviews.map( function mapRequiredStatus( review ) { return [ review.id, 'phase1_required' ]; } ),
		...statusLock.phase1DeferredReviews.map( function mapDeferredStatus( review ) { return [ review.id, 'deferred_missing_capability' ]; } )
	] );
	const seenIds = new Set();
	const normalizedRequired = [];
	const normalizedDeferred = [];
	let externalAssetCount = 0;
	let previousOrder = -1;
	for ( let proposalIndex = 0; proposalIndex < proposals.length; proposalIndex ++ ) {

		const normalized = normalizeProposal( proposals[ proposalIndex ], upstreamRoot, `proposals[${ proposalIndex }]` );
		const exampleId = normalized.review.id;
		if ( seenIds.has( exampleId ) ) throw new Error( `Proposal repeats ${ exampleId }.` );
		seenIds.add( exampleId );
		const lockedStatus = lockedStatuses.get( exampleId );
		if ( replaceLocked ) {

			if ( lockedStatus === undefined ) throw new Error( `${ exampleId } has no locked Phase-1 review to replace.` );
			if ( lockedStatus !== normalized.status ) throw new Error( `${ exampleId } replacement cannot change its locked Phase-1 status.` );

		} else if ( lockedStatus !== undefined ) throw new Error( `${ exampleId } already has a locked Phase-1 status.` );
		const order = manifestOrder.get( exampleId );
		if ( order === undefined ) throw new Error( `${ exampleId } is absent from the pinned 588-example inventory.` );
		if ( order <= previousOrder ) throw new Error( `Proposal order is not deterministic at ${ exampleId }.` );
		previousOrder = order;
		externalAssetCount += normalized.externalAssetCount;
		if ( normalized.status === 'phase1_required' ) normalizedRequired.push( normalized.review );
		else normalizedDeferred.push( normalized.review );

	}
	const scope = proposal.scope;
	if ( scope === null || typeof scope !== 'object' ) throw new Error( 'Proposal scope must contain deterministic counts.' );
	if ( scope.auditedPendingExamples !== proposals.length ) throw new Error( 'Proposal scope auditedPendingExamples count is stale.' );
	if ( scope.phase1Required !== normalizedRequired.length ) throw new Error( 'Proposal scope phase1Required count is stale.' );
	if ( scope.deferredMissingCapability !== normalizedDeferred.length ) throw new Error( 'Proposal scope deferredMissingCapability count is stale.' );

	const candidateStatusLock = structuredClone( statusLock );
	if ( replaceLocked ) {

		candidateStatusLock.phase1RequiredReviews = candidateStatusLock.phase1RequiredReviews.filter( function keepsUnreplacedRequired( review ) { return ! seenIds.has( review.id ); } );
		candidateStatusLock.phase1DeferredReviews = candidateStatusLock.phase1DeferredReviews.filter( function keepsUnreplacedDeferred( review ) { return ! seenIds.has( review.id ); } );

	}
	candidateStatusLock.phase1RequiredReviews.push( ...normalizedRequired );
	candidateStatusLock.phase1DeferredReviews.push( ...normalizedDeferred );
	validateStatusLock( candidateStatusLock, capabilityAudit, adjudication, upstreamRoot, repositoryRoot );
	const baselineManifest = buildManifest( inventory, exclusions );
	const generated = buildPhase1ManifestLock( baselineManifest, capabilityAudit, adjudication, candidateStatusLock );
	generated.manifest = applySingleSamplePolicy( generated.manifest );
	const manifestValidation = validateManifest( generated.manifest, exclusions );
	if ( manifestValidation.failures.length > 0 ) throw new Error( `Candidate manifest failed validation:\n${ manifestValidation.failures.join( '\n' ) }` );
	const schemaErrors = validateJsonSchema( generated.manifest, manifestSchema );
	if ( schemaErrors.length > 0 ) throw new Error( `Candidate manifest failed JSON Schema validation:\n${ schemaErrors.map( function formatSchemaError( error ) { return `${ error.instancePath || '/' }: ${ error.message }`; } ).join( '\n' ) }` );
	const renderSetLintOptions = inputs.geometryGroupSceneRootsByExample === undefined
		? {}
		: { geometryGroupSceneRootsByExample: inputs.geometryGroupSceneRootsByExample };
	const renderSetLint = lintRenderSetPolicy( generated.manifest, false, renderSetLintOptions );
	if ( renderSetLint.failures.length > 0 ) throw new Error( `Candidate manifest failed RenderSet lint:\n${ renderSetLint.failures.join( '\n' ) }` );
	return {
		proposalCount: proposals.length,
		phase1Required: normalizedRequired.length,
		deferredMissingCapability: normalizedDeferred.length,
		externalAssetCount,
		pendingAfterMerge: generated.manifest.counts.auditPending,
		replacedLocked: replaceLocked ? proposals.length : 0,
		normalizedRequired,
		normalizedDeferred
	};

}

/** Parses explicit proposal validator paths and rejects every unknown option. */
export function parseAuditProposalArguments( argumentsList ) {

	const defaults = {
		proposalPath: null,
		upstreamRoot: null,
		replaceLocked: false,
		inventoryPath: defaultInventoryPath,
		exclusionsPath: defaultExclusionsPath,
		auditPath: defaultAuditPath,
		adjudicationPath: defaultAdjudicationPath,
		statusLockPath: defaultStatusLockPath,
		schemaPath: defaultSchemaPath
	};
	const options = new Map( [
		[ '--proposal', 'proposalPath' ],
		[ '--upstream-root', 'upstreamRoot' ],
		[ '--inventory', 'inventoryPath' ],
		[ '--exclusions', 'exclusionsPath' ],
		[ '--audit', 'auditPath' ],
		[ '--adjudication', 'adjudicationPath' ],
		[ '--status-lock', 'statusLockPath' ],
		[ '--schema', 'schemaPath' ]
	] );
	for ( let argumentIndex = 0; argumentIndex < argumentsList.length; argumentIndex ++ ) {

		if ( argumentsList[ argumentIndex ] === '--replace-locked' ) {

			defaults.replaceLocked = true;
			continue;

		}
		const property = options.get( argumentsList[ argumentIndex ] );
		if ( property === undefined ) throw new Error( `Unknown argument: ${ argumentsList[ argumentIndex ] }.` );
		const value = argumentsList[ argumentIndex + 1 ];
		if ( value === undefined || value.startsWith( '--' ) ) throw new Error( `Missing value for ${ argumentsList[ argumentIndex ] }.` );
		defaults[ property ] = path.resolve( value );
		argumentIndex ++;

	}
	if ( defaults.proposalPath === null || defaults.upstreamRoot === null ) throw new Error( '--proposal and --upstream-root are required.' );
	return defaults;

}

/** Verifies the explicit Three.js checkout and executes the standalone proposal gate. */
function main() {

	const options = parseAuditProposalArguments( process.argv.slice( 2 ) );
	const revision = execFileSync( 'git', [ '-C', options.upstreamRoot, 'rev-parse', 'HEAD' ], { encoding: 'utf8' } ).trim();
	if ( revision !== THREE_R185_COMMIT ) throw new Error( `Three.js upstream revision must be ${ THREE_R185_COMMIT }, got ${ revision }.` );
	const result = validateAuditProposal( {
		proposal: readJson( options.proposalPath ),
		upstreamRoot: options.upstreamRoot,
		inventory: readJson( options.inventoryPath ),
		exclusions: readJson( options.exclusionsPath ),
		capabilityAudit: readJson( options.auditPath ),
		adjudication: readJson( options.adjudicationPath ),
		statusLock: readJson( options.statusLockPath ),
		manifestSchema: readJson( options.schemaPath ),
		replaceLocked: options.replaceLocked
	} );
	console.log( `Audit proposal valid: reviews=${ result.proposalCount } required=${ result.phase1Required } deferred=${ result.deferredMissingCapability } external_assets=${ result.externalAssetCount } pending_after_merge=${ result.pendingAfterMerge } replaced_locked=${ result.replacedLocked }.` );

}

if ( process.argv[ 1 ] && path.resolve( process.argv[ 1 ] ) === fileURLToPath( import.meta.url ) ) main();
