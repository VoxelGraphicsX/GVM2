import { countStatuses } from './manifest_common.mjs';

const BASE_RENDER_SET_COMPONENT_SCHEMA = [
	{ name: 'vertices', kind: 'buffer', role: 'vertex' },
	{ name: 'indices', kind: 'buffer', role: 'index' },
	{ name: 'objects', kind: 'buffer', role: 'object' },
	{ name: 'instances', kind: 'buffer', role: 'instance' },
	{ name: 'materials', kind: 'buffer', role: 'material' }
];

const MANIFEST_RENDER_FIELDS = [
	'renderSetPolicy', 'renderSetReasons', 'sceneRoots', 'renderableObjectCount',
	'containsInstancing', 'containsHierarchy', 'containsLod', 'containsDynamicObjects',
	'containsMultipleMaterials', 'containsGeometryGroups', 'loaderRenderableObjectCount',
	'scenePasses', 'screenPasses', 'renderSetType', 'componentSchema'
];

const CONFIRMED_TRIGGER_NAMES = [ 'instancing', 'hierarchy', 'lod', 'geometry_groups' ];

/** Returns a stable PascalCase identifier fragment for generated RenderSet plans. */
function toPascalCase( value ) {

	return value.split( /[^A-Za-z0-9]+/ ).filter( Boolean ).map( function capitalizePart( part ) {

		return `${ part[ 0 ].toUpperCase() }${ part.slice( 1 ) }`;

	} ).join( '' );

}

/** Formats one upstream evidence record for the manifest's compact evidence list. */
function formatSourceEvidence( evidence ) {

	return `${ evidence.sourcePath }:${ evidence.line}: ${ evidence.excerpt }`;

}

/** Returns dynamic-object evidence that proves remove or reparent semantics, excluding visibility-only matches. */
function hasConfirmedDynamicObjectTrigger( trigger ) {

	return trigger.evidence.some( function hasLifecycleMutation( evidence ) {

		return evidence.detector === 'named_object_remove' || evidence.detector === 'object_reparent';

	} );

}

/** Derives only RenderSet reasons that are safe to lock without runtime object-count or loader-asset assumptions. */
function deriveConfirmedRenderSetReasons( auditExample, adjudicationDecisions ) {

	const reasons = [];
	for ( const triggerName of CONFIRMED_TRIGGER_NAMES ) {

		if ( auditExample.renderSetAudit.triggers[ triggerName ].candidate ) reasons.push( triggerName );

	}
	if ( hasConfirmedDynamicObjectTrigger( auditExample.renderSetAudit.triggers.dynamic_objects ) ) reasons.push( 'dynamic_objects' );
	if ( adjudicationDecisions.some( function usesBillboardInstances( decision ) {

		return decision.capability === 'point_size_builtin' && decision.decision === 'expressible_existing_dsl';

	} ) && ! reasons.includes( 'instancing' ) ) reasons.push( 'instancing' );
	return reasons;

}

/** Creates a stable minimum one-RenderSet-per-Scene plan for a confirmed complex source. */
function createConfirmedRenderSetPlan( exampleId, auditExample, reasons ) {

	if ( reasons.length === 0 || auditExample.renderSetAudit.sceneRoots.length === 0 ) return null;
	const renderSetType = `${ toPascalCase( exampleId ) }SceneRenderSet`;
	const sceneRoots = auditExample.renderSetAudit.sceneRoots.map( function mapSceneRoot( root ) {

		return { name: root.name, renderSetRuntimeInstanceCount: 1, renderSetType };

	} );
	const scenePasses = sceneRoots.map( function mapMainScenePass( root ) {

		return {
			name: `${ root.name }-main`,
			renderClass: `${ toPascalCase( exampleId ) }${ toPascalCase( root.name ) }MainPass`,
			sceneRoot: root.name,
			renderSetBindingCount: 1,
			usesStandaloneGeometry: false,
			usesExplicitDrawCount: false
		};

	} );
	return {
		renderSetPolicy: 'required',
		renderSetReasons: reasons,
		sceneRoots,
		renderableObjectCount: null,
		containsInstancing: reasons.includes( 'instancing' ) ? true : null,
		containsHierarchy: reasons.includes( 'hierarchy' ) ? true : null,
		containsLod: reasons.includes( 'lod' ) ? true : null,
		containsDynamicObjects: reasons.includes( 'dynamic_objects' ) ? true : null,
		containsMultipleMaterials: null,
		containsGeometryGroups: reasons.includes( 'geometry_groups' ) ? true : null,
		loaderRenderableObjectCount: null,
		scenePasses,
		screenPasses: [],
		renderSetType,
		componentSchema: BASE_RENDER_SET_COMPONENT_SCHEMA.map( function cloneComponent( component ) { return { ...component }; } )
	};

}

/** Copies all manifest render-policy fields from a complete manual review record. */
function copyReviewedRenderFields( review ) {

	const fields = {};
	for ( const fieldName of MANIFEST_RENDER_FIELDS ) {

		if ( Object.hasOwn( review, fieldName ) ) fields[ fieldName ] = structuredClone( review[ fieldName ] );

	}
	if ( fields.renderSetPolicy === 'required' && ! Object.hasOwn( fields, 'componentSchema' ) ) {

		fields.componentSchema = BASE_RENDER_SET_COMPONENT_SCHEMA.map( function cloneComponent( component ) { return { ...component }; } );

	}
	return fields;

}

/** Builds the supported capability record for a fully reviewed phase-1-required entry. */
function createRequiredCapabilityAudit( review ) {

	return {
		state: 'supported',
		gpuWorkDslOnly: true,
		requiresNewPublicCapability: false,
		requiredCapabilities: review.requiredCapabilities,
		availableCapabilities: review.availableCapabilities,
		missingCapabilities: [],
		evidence: [
			...review.upstreamSourceEvidence.map( formatSourceEvidence ),
			...review.repositoryCapabilityEvidence.map( formatSourceEvidence )
		],
		notes: [ ...review.notes, 'Complete entry HTML was manually reviewed against the frozen public DSL/RenderSet/RHI surface.' ]
	};

}

/** Builds a complete deferred audit and six-field evidence record from one or more deferred adjudications. */
function createDeferredRecords( deferredDecisions ) {

	const sourceEvidence = deferredDecisions.flatMap( function collectEvidence( decision ) {

		return decision.upstreamSourceEvidence.map( formatSourceEvidence );

	} );
	const uniqueSourceEvidence = [ ...new Set( sourceEvidence ) ];
	const missingCapabilities = deferredDecisions.map( function mapCapability( decision ) { return decision.capability; } );
	const joinField = function joinDecisionField( fieldName ) {

		return deferredDecisions.map( function mapDecisionField( decision ) { return decision[ fieldName ]; } ).join( ' | ' );

	};
	return {
		capabilityAudit: {
			state: 'deferred',
			gpuWorkDslOnly: true,
			requiresNewPublicCapability: true,
			requiredCapabilities: missingCapabilities,
			availableCapabilities: [],
			missingCapabilities,
			evidence: [ ...uniqueSourceEvidence ],
			notes: [ 'Deferred only by complete manual adjudication; workload and missing sample implementation are not accepted reasons.' ]
		},
		deferredEvidence: {
			reasonCode: deferredDecisions[ 0 ].reasonCode,
			upstreamSourceEvidence: [ ...uniqueSourceEvidence ],
			missingCapability: missingCapabilities.join( ', ' ),
			whyCurrentDslCannotExpressIt: joinField( 'whyCurrentDslCannotExpressIt' ),
			minimumFutureApi: joinField( 'minimumFutureApi' ),
			reentryTest: joinField( 'reentryTest' )
		}
	};

}

/** Builds a complete deferred audit from a full-entry review of a capability absent from detector candidates. */
function createReviewedDeferredRecords( review ) {

	const upstreamEvidence = review.upstreamSourceEvidence.map( formatSourceEvidence );
	const repositoryEvidence = review.repositoryCapabilityEvidence.map( formatSourceEvidence );
	return {
		capabilityAudit: {
			state: 'deferred',
			gpuWorkDslOnly: true,
			requiresNewPublicCapability: true,
			requiredCapabilities: [ ...review.requiredCapabilities ],
			availableCapabilities: [ ...review.availableCapabilities ],
			missingCapabilities: [ ...review.missingCapabilities ],
			evidence: [ ...upstreamEvidence, ...repositoryEvidence ],
			notes: [
				...review.notes,
				'Complete entry HTML was manually reviewed against the frozen public DSL/RenderSet/RHI surface.'
			]
		},
		deferredEvidence: {
			reasonCode: review.reasonCode,
			upstreamSourceEvidence: [ ...upstreamEvidence ],
			missingCapability: review.missingCapabilities.join( ', ' ),
			whyCurrentDslCannotExpressIt: review.whyCurrentDslCannotExpressIt,
			minimumFutureApi: review.minimumFutureApi,
			reentryTest: review.reentryTest
		}
	};

}

/** Builds an honest pending capability record that preserves completed candidate decisions without claiming full support. */
function createPendingCapabilityAudit( auditExample, adjudicationDecisions, blockers ) {

	const expressibleCapabilities = adjudicationDecisions.filter( function isExpressible( decision ) {

		return decision.decision === 'expressible_existing_dsl';

	} ).map( function mapCapability( decision ) { return decision.capability; } );
	const evidence = adjudicationDecisions.flatMap( function collectDecisionEvidence( decision ) {

		return decision.upstreamSourceEvidence.map( formatSourceEvidence );

	} );
	if ( blockers.includes( 'derived_point_billboard_instancing_requires_RenderEntityInstanceID' ) ) {

		evidence.push( 'Phase-1 point expansion requires [[RenderEntityInstanceID]] on the Scene RenderSet vertex path.' );

	}
	return {
		state: 'pending',
		gpuWorkDslOnly: true,
		requiresNewPublicCapability: null,
		requiredCapabilities: [ ...new Set( expressibleCapabilities ) ],
		availableCapabilities: [ ...new Set( expressibleCapabilities ) ],
		missingCapabilities: [],
		evidence: [ ...new Set( evidence ) ],
		notes: [
			'Candidate adjudication does not prove full-entry GPU semantic support.',
			`Remaining lock blockers: ${ blockers.join( ', ') }.`
		]
	};

}

/** Adds one blocker to a stable per-code report index. */
function addBlocker( blockerIndex, exampleId, blockerCode ) {

	blockerIndex[ blockerCode ] ??= [];
	blockerIndex[ blockerCode ].push( exampleId );

}

/** Derives concrete pending blockers from audit evidence and the current RenderSet lock state. */
function derivePendingBlockers( auditExample, adjudicationDecisions, renderFields ) {

	const blockers = [ 'full_entry_gpu_semantic_review_missing', 'manual_only_capability_review_missing', 'canonical_scenarios_unlocked' ];
	if ( adjudicationDecisions.length > 0 ) blockers.push( 'candidate_adjudication_is_not_full_entry_support' );
	if ( auditExample.auxiliaryMissingCapabilitySignals.length > 0 ) blockers.push( 'auxiliary_capability_path_unresolved' );
	if ( renderFields.renderSetPolicy === null ) blockers.push( 'render_set_policy_unresolved' );
	if ( renderFields.renderableObjectCount === null ) blockers.push( 'runtime_renderable_count_unresolved' );
	if ( auditExample.features.loader.coreDetected ) blockers.push( 'canonical_loader_asset_renderable_count_unresolved' );
	if ( auditExample.renderSetAudit.sceneRoots.length > 0 ) blockers.push( 'complete_scene_pass_inventory_unresolved' );
	if ( auditExample.features.postprocessing.coreDetected ) blockers.push( 'complete_screen_pass_inventory_unresolved' );
	if ( auditExample.features.renderable_construction.coreDetected && auditExample.renderSetAudit.sceneRoots.length === 0 ) blockers.push( 'scene_root_runtime_mapping_unresolved' );
	if ( adjudicationDecisions.some( function hasPointExpansion( decision ) {

		return decision.capability === 'point_size_builtin' && decision.decision === 'expressible_existing_dsl';

	} ) ) blockers.push( 'derived_point_billboard_instancing_requires_RenderEntityInstanceID' );
	if ( renderFields.renderSetPolicy === 'required' ) blockers.push( 'render_set_optional_component_schema_unresolved' );
	return blockers;

}

/** Applies reviewed status decisions and conservative RenderSet strategy locks to a baseline manifest. */
export function buildPhase1ManifestLock( baselineManifest, capabilityAudit, adjudication, statusLock ) {

	if ( baselineManifest.upstream.commit !== statusLock.upstreamCommit || capabilityAudit.upstream.commit !== statusLock.upstreamCommit || adjudication.upstream.commit !== statusLock.upstreamCommit ) {

		throw new Error( 'Manifest lock inputs must use the same pinned Three.js commit.' );

	}
	const auditById = new Map( capabilityAudit.examples.map( function mapAudit( example ) { return [ example.id, example ]; } ) );
	const adjudicationById = new Map();
	for ( const decision of adjudication.decisions ) {

		const decisions = adjudicationById.get( decision.exampleId ) ?? [];
		decisions.push( decision );
		adjudicationById.set( decision.exampleId, decisions );

	}
	const requiredReviews = new Map( statusLock.phase1RequiredReviews.map( function mapReview( review ) { return [ review.id, review ]; } ) );
	const deferredReviews = new Map( ( statusLock.phase1DeferredReviews ?? [] ).map( function mapReview( review ) { return [ review.id, review ]; } ) );
	const deferredReclassifications = new Map( ( statusLock.phase1DeferredReclassifications ?? [] ).map( function mapReview( review ) { return [ review.id, review ]; } ) );
	const renderSetOverrides = new Map( statusLock.renderSetOverrides.map( function mapOverride( review ) { return [ review.id, review ]; } ) );
	for ( const [ reviewId, deferredReview ] of deferredReclassifications ) {

		const requiredReview = requiredReviews.get( reviewId );
		if ( ! requiredReview ) throw new Error( `${ reviewId } cannot be reclassified without a complete required review.` );
		if ( deferredReviews.has( reviewId ) ) throw new Error( `${ reviewId } cannot have both a deferred review and a deferred reclassification.` );
		deferredReviews.set( reviewId, {
			...requiredReview,
			...deferredReview,
			requiredCapabilities: [ ...new Set( [
				...requiredReview.requiredCapabilities,
				...( deferredReview.missingCapabilities ?? [] )
			] ) ],
			notes: [
				...requiredReview.notes,
				...( deferredReview.notes ?? [] )
			]
		} );
		requiredReviews.delete( reviewId );

	}
	for ( const reviewId of requiredReviews.keys() ) {

		if ( deferredReviews.has( reviewId ) ) throw new Error( `${ reviewId } cannot be both phase1_required and manually deferred.` );

	}
	const blockerIndex = {};
	const examples = baselineManifest.examples.map( function lockExample( baselineExample ) {

		const example = structuredClone( baselineExample );
		if ( example.status === 'excluded_upstream' ) return example;
		const auditExample = auditById.get( example.id );
		if ( ! auditExample ) throw new Error( `Capability audit is missing ${ example.id }.` );
		const decisions = adjudicationById.get( example.id ) ?? [];
		const deferredDecisions = decisions.filter( function isDeferred( decision ) { return decision.decision === 'deferred'; } );
		const confirmedReasons = deriveConfirmedRenderSetReasons( auditExample, decisions );
		let renderFields = createConfirmedRenderSetPlan( example.id, auditExample, confirmedReasons ) ?? {
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
			loaderRenderableObjectCount: auditExample.features.loader.coreDetected ? null : 0,
			scenePasses: [],
			screenPasses: [],
			renderSetType: null,
			componentSchema: []
		};
		const renderSetOverride = renderSetOverrides.get( example.id );
		if ( renderSetOverride ) renderFields = { ...renderFields, ...copyReviewedRenderFields( renderSetOverride ) };
		Object.assign( example, renderFields );

		const requiredReview = requiredReviews.get( example.id );
		const deferredReview = deferredReviews.get( example.id );
		if ( deferredReview && deferredDecisions.length > 0 ) {

			throw new Error( `${ example.id } duplicates detector adjudication with a manual deferred review.` );

		}
		if ( deferredReview ) {

			example.status = 'deferred_missing_capability';
			const deferredRecords = createReviewedDeferredRecords( deferredReview );
			example.capabilityAudit = deferredRecords.capabilityAudit;
			example.deferredEvidence = deferredRecords.deferredEvidence;
			Object.assign( example, copyReviewedRenderFields( deferredReview ) );
			example.dslShard = deferredReview.dslShard ?? null;
			example.scenarios = structuredClone( deferredReview.scenarios ?? [] );

		} else if ( deferredDecisions.length > 0 ) {

			example.status = 'deferred_missing_capability';
			const deferredRecords = createDeferredRecords( deferredDecisions );
			example.capabilityAudit = deferredRecords.capabilityAudit;
			example.deferredEvidence = deferredRecords.deferredEvidence;

		} else if ( requiredReview ) {

			example.status = 'phase1_required';
			example.capabilityAudit = createRequiredCapabilityAudit( requiredReview );
			example.deferredEvidence = null;
			Object.assign( example, copyReviewedRenderFields( requiredReview ) );
			example.dslShard = requiredReview.dslShard;
			example.scenarios = structuredClone( requiredReview.scenarios );

		} else {

			example.status = 'audit_pending';
			example.deferredEvidence = null;
			const blockers = derivePendingBlockers( auditExample, decisions, renderFields );
			example.capabilityAudit = createPendingCapabilityAudit( auditExample, decisions, blockers );
			for ( const blocker of blockers ) addBlocker( blockerIndex, example.id, blocker );

		}
		if ( example.renderSetPolicy === 'required' && example.containsInstancing && ! example.capabilityAudit.evidence.some( function hasBuiltinEvidence( evidence ) { return evidence.includes( 'RenderEntityInstanceID' ); } ) ) {

			example.capabilityAudit.evidence.push( 'Scene RenderSet instancing path requires [[RenderEntityInstanceID]].' );

		}
		return example;

	} );

	for ( const reviewId of requiredReviews.keys() ) if ( ! auditById.has( reviewId ) ) throw new Error( `Required review references unknown example ${ reviewId }.` );
	for ( const reviewId of deferredReviews.keys() ) if ( ! auditById.has( reviewId ) ) throw new Error( `Deferred review references unknown example ${ reviewId }.` );
	for ( const reviewId of deferredReclassifications.keys() ) if ( ! auditById.has( reviewId ) ) throw new Error( `Deferred reclassification references unknown example ${ reviewId }.` );
	for ( const reviewId of renderSetOverrides.keys() ) if ( ! auditById.has( reviewId ) ) throw new Error( `RenderSet override references unknown example ${ reviewId }.` );
	const manifest = { ...baselineManifest, counts: countStatuses( examples ), examples };
	const renderSetPolicyCounts = { required: 0, notRequired: 0, unresolved: 0 };
	for ( const example of examples ) {

		if ( example.status === 'excluded_upstream' ) continue;
		if ( example.renderSetPolicy === 'required' ) renderSetPolicyCounts.required ++;
		else if ( example.renderSetPolicy === 'not-required' ) renderSetPolicyCounts.notRequired ++;
		else renderSetPolicyCounts.unresolved ++;

	}
	const blockerCounts = Object.fromEntries( Object.entries( blockerIndex ).map( function mapBlockerCount( [ code, exampleIds ] ) {

		return [ code, exampleIds.length ];

	} ) );
	const report = {
		schemaVersion: 1,
		phase: 'three-r185-phase1',
		upstreamCommit: statusLock.upstreamCommit,
		statusCounts: manifest.counts,
		renderSetPolicyCounts,
		lockedPhase1RequiredIds: [ ...requiredReviews.keys() ],
		lockedDeferredIds: examples.filter( function isDeferred( example ) { return example.status === 'deferred_missing_capability'; } ).map( function mapId( example ) { return example.id; } ),
		readyForStrictGate: manifest.counts.auditPending === 0,
		blockerCounts,
		blockers: Object.fromEntries( Object.entries( blockerIndex ).map( function mapBlockerIds( [ code, exampleIds ] ) {

			return [ code, { count: exampleIds.length, exampleIds } ];

		} ) )
	};
	return { manifest, report };

}
