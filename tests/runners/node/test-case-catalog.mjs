const catalog = new Map([
  ['MultipleElementsTests.StoresValuesFromInitializerList', {
    subsystem: 'RHI API Helpers',
    level: 'White-box Unit',
    kind: 'Container Semantics',
    description: 'Verifies that `MultipleElements<T>` preserves ordered values from initializer-list construction.',
    content: 'Constructs a small container directly from literal values and checks the retained element order and count.',
    validates: [
      'Initializer-list construction keeps insertion order.',
      'Container length reflects the number of provided elements.',
      'Indexed access returns the stored payload without mutation.'
    ],
    watchouts: [
      'Regressions here can silently corrupt descriptor arrays and bind-group entry packing.'
    ]
  }],
  ['MultipleElementsTests.SupportsScalarAndRangeAssignment', {
    subsystem: 'RHI API Helpers',
    level: 'White-box Unit',
    kind: 'Container Mutation',
    description: 'Checks scalar replacement and whole-range replacement behavior for `MultipleElements<T>`.',
    content: 'Writes a scalar into the container, then replaces it with a fixed-size array and verifies both forms.',
    validates: [
      'Scalar assignment collapses the container to one element.',
      'Range assignment rebuilds the container with the replacement sequence.',
      'Indexing remains stable after reassignment.'
    ],
    watchouts: [
      'Descriptor builders rely on this behavior when promoting single values to multi-binding forms.'
    ]
  }],
  ['MultipleElementsTests.SupportsVariadicConstructionAndContiguousAccess', {
    subsystem: 'RHI API Helpers',
    level: 'White-box Unit',
    kind: 'Container Layout',
    description: 'Validates variadic construction and contiguous storage exposure for `MultipleElements<T>`.',
    content: 'Constructs the helper from multiple scalar arguments and checks contiguous access through the exposed raw pointer.',
    validates: [
      'Variadic construction preserves the provided element sequence.',
      'The container exposes contiguous storage through `data()`.',
      'Indexing and raw-pointer access agree on stored values.'
    ],
    watchouts: [
      'Descriptor upload code frequently depends on this helper behaving like a tiny contiguous array.'
    ]
  }],
  ['BufferRangeTests.WholeSizeClampsToBufferStorage', {
    subsystem: 'RHI Core Types',
    level: 'White-box Unit',
    kind: 'Range Math',
    description: 'Confirms that an unspecified `BufferRange` size expands to the full storage size of the underlying buffer.',
    content: 'Creates a dummy buffer with a known size, then constructs a whole-range view and checks offset and size.',
    validates: [
      'Whole-size sentinel is resolved against concrete buffer storage.',
      'Offset remains unchanged when defaulting to whole-size.',
      'No extra allocation or mutation is required to compute the range.'
    ],
    watchouts: [
      'Incorrect whole-size resolution can poison copy and upload commands across the RHI.'
    ]
  }],
  ['BufferRangeTests.ExplicitSizeStaysUnchangedWhenInsideBounds', {
    subsystem: 'RHI Core Types',
    level: 'White-box Unit',
    kind: 'Range Math',
    description: 'Ensures that explicit sub-ranges inside the buffer bounds are preserved exactly.',
    content: 'Creates a dummy buffer, requests an in-bounds range with non-zero offset, and verifies the stored values.',
    validates: [
      'Explicit offsets are preserved.',
      'Explicit sizes remain unchanged when already in range.',
      'Sub-range construction does not normalize or expand valid ranges.'
    ],
    watchouts: [
      'Descriptor normalization bugs here can break indirect copies and bind-group suballocations.'
    ]
  }],
  ['BufferRangeTests.ExplicitZeroSizeRemainsZero', {
    subsystem: 'RHI Core Types',
    level: 'White-box Unit',
    kind: 'Range Edge Case',
    description: 'Confirms that an explicit zero-sized buffer range stays zero-sized instead of being expanded implicitly.',
    content: 'Constructs a dummy buffer range with a non-zero offset and an explicit size of zero, then checks the stored values.',
    validates: [
      'Zero-sized ranges remain explicit.',
      'Offsets are preserved even when the range length is zero.',
      'No hidden expansion occurs for empty range requests.'
    ],
    watchouts: [
      'Zero-length copies and binding slices appear in real descriptor-building code and must stay stable.'
    ]
  }],
  ['BufferRangeTests.WholeSizeWithOffsetUsesRemainingBytes', {
    subsystem: 'RHI Core Types',
    level: 'White-box Unit',
    kind: 'Known Bug Exposure',
    description: 'Asserts the intended `BufferRange(buffer, offset, WholeSize)` contract: the computed size should cover only the remaining bytes after the offset.',
    content: 'Builds a whole-size buffer view with a non-zero offset and checks whether the stored byte count shrinks to the remaining region instead of the full buffer size.',
    requirements: [
      'A whole-size range starting at a non-zero offset must resolve to `bufferSize - offset`.',
      'Offset normalization must not silently widen the accessible byte span.',
      'The test should stay red until the constructor clamps against remaining capacity instead of total capacity.'
    ],
    validates: [
      'Default-size sentinel math respects the supplied offset.',
      'Range metadata matches the bytes that downstream copy/bind operations may legally access.',
      'The test exposes a concrete correctness bug rather than a style issue.'
    ],
    observability: [
      'Failure appears as a byte-count mismatch in the parsed GoogleTest assertion output.',
      'No GPU execution is required; the issue is visible from pure constructor state.'
    ],
    watchouts: [
      'If this stays broken, buffer uploads and vertex/index bindings can read or write past the intended logical slice.'
    ]
  }],
  ['ResourcePoolTests.AllocatesAndResolvesHandle', {
    subsystem: 'Core Resource Pool',
    level: 'White-box Unit',
    kind: 'Handle Lifetime',
    description: 'Validates that a newly allocated handle resolves to the expected resource payload.',
    content: 'Allocates a dummy resource with a label, reads it back through the handle interface, and then frees it.',
    validates: [
      'Allocation returns a non-null handle.',
      'Handle dereference resolves the original resource object.',
      'Resource metadata remains intact before the free path.'
    ],
    watchouts: [
      'Broken handle lookup will destabilize every handle-backed RHI resource type.'
    ]
  }],
  ['ResourcePoolTests.FreedHandleBecomesStale', {
    subsystem: 'Core Resource Pool',
    level: 'White-box Unit',
    kind: 'Handle Lifetime',
    description: 'Checks generation-based invalidation after a resource is freed.',
    content: 'Allocates a dummy resource, frees it immediately, and verifies the stale handle no longer resolves.',
    validates: [
      'Free increments generation state for the slot.',
      'Old handles stop resolving after release.',
      'Use-after-free through stale handles is blocked at lookup time.'
    ],
    watchouts: [
      'Generation bugs are a common source of silent stale-handle reuse.'
    ]
  }],
  ['ResourcePoolTests.ReusesFreedSlotWithFreshGeneration', {
    subsystem: 'Core Resource Pool',
    level: 'White-box Unit',
    kind: 'Slot Reuse',
    description: 'Checks that a freed slot can be reused while the stale generation remains invalid.',
    content: 'Allocates a resource, frees it, allocates a replacement, and verifies the new handle resolves while the old one stays stale.',
    validates: [
      'Freed slots are reusable by later allocations.',
      'Generation increments keep the old handle invalid.',
      'Replacement resources are visible through the new handle only.'
    ],
    watchouts: [
      'Slot reuse is a common source of subtle stale-handle bugs in resource managers.'
    ]
  }],
  ['ResourcePoolTests.HandleResetMarksItNull', {
    subsystem: 'Core Resource Pool',
    level: 'White-box Unit',
    kind: 'Handle API',
    description: 'Verifies the explicit handle reset path marks a handle as null without touching the pool.',
    content: 'Allocates a resource handle, resets the handle object itself, and checks its null-state contract.',
    validates: [
      'Handle-local reset clears the pool association.',
      'Null-state checks become true after reset.',
      'Reset does not require mutating the owning pool.'
    ],
    watchouts: [
      'This matters for higher-level wrappers that cache and invalidate handles manually.'
    ]
  }],
  ['ProgressiveDataTests.CreatesAndResizesWithConfiguredIncrement', {
    subsystem: 'Core Progressive Data',
    level: 'White-box Unit',
    kind: 'Dynamic Growth',
    description: 'Verifies incremental growth semantics for `ProgressiveData<T>`.',
    content: 'Creates the container with a fixed growth granularity, writes beyond the initial capacity, and checks expansion.',
    validates: [
      'Initial creation honors the configured increment size.',
      'Out-of-range indexed writes trigger expansion.',
      'Previously written values survive a resize.'
    ],
    watchouts: [
      'Incorrect resize behavior can corrupt component storage and renderer-side staging buffers.'
    ]
  }],
  ['ProgressiveDataTests.FillRangeUpdatesElementsInPlace', {
    subsystem: 'Core Progressive Data',
    level: 'White-box Unit',
    kind: 'Bulk Update',
    description: 'Ensures in-place range fill updates only the requested slice.',
    content: 'Creates zero-initialized storage, fills a middle range, and confirms unaffected neighbors stay unchanged.',
    validates: [
      'Bulk range writes affect only the requested segment.',
      'Data before and after the filled slice remains untouched.',
      'The operation works without reallocation when capacity already exists.'
    ],
    watchouts: [
      'Incorrect fill bounds can scramble render component payloads across entities.'
    ]
  }],
  ['ProgressiveDataTests.ClearResetsLengthAndByteSize', {
    subsystem: 'Core Progressive Data',
    level: 'White-box Unit',
    kind: 'Lifecycle Reset',
    description: 'Validates that clearing progressive storage removes logical contents and resets the reported byte size.',
    content: 'Expands the storage, verifies it contains data, then clears it and checks length and byte size collapse to zero.',
    validates: [
      'Clear removes logical contents.',
      'Byte-size accounting tracks the cleared state.',
      'A previously grown container returns to an empty logical footprint.'
    ],
    watchouts: [
      'Incorrect clear semantics can leak stale render component payloads across frames.'
    ]
  }],
  ['ProgressiveDataTests.ResizePreservesPreviouslyWrittenElements', {
    subsystem: 'Core Progressive Data',
    level: 'White-box Unit',
    kind: 'Resize Stability',
    description: 'Checks that earlier writes survive later growth operations.',
    content: 'Writes values into a small storage block, grows it via an out-of-range write, and verifies the original values remain intact.',
    validates: [
      'Growth preserves earlier element values.',
      'Resized storage reaches the expected aligned length.',
      'New writes coexist with previously written payloads.'
    ],
    watchouts: [
      'This catches value loss during internal vector growth and reallocation.'
    ]
  }],
  ['StagingLinearAllocatorTests.AppendsDataWithConfiguredAlignment', {
    subsystem: 'Core Staging Allocator',
    level: 'White-box Unit',
    kind: 'Alignment and Packing',
    description: 'Verifies that CPU staging allocations honor explicit alignment requirements and keep payload bytes intact.',
    content: 'Appends two small payloads with a 16-byte alignment boundary and validates the resulting offsets and stored words.',
    validates: [
      'Aligned append offsets are deterministic.',
      'Padding insertion does not corrupt appended data.',
      'Allocator size reflects alignment gaps plus payload bytes.'
    ],
    watchouts: [
      'Incorrect alignment here can silently break GPU upload offsets and bind-group data layouts.'
    ]
  }],
  ['StagingLinearAllocatorTests.ResetClearsLogicalContents', {
    subsystem: 'Core Staging Allocator',
    level: 'White-box Unit',
    kind: 'Allocator Lifecycle',
    description: 'Checks that allocator reset drops logical contents so the arena can be reused safely.',
    content: 'Appends data, verifies the arena is non-empty, then resets it and checks that the logical size returns to zero.',
    validates: [
      'Reset clears the logical payload size.',
      'Allocator emptiness reflects reset state.',
      'Arena reuse semantics remain explicit and predictable.'
    ],
    watchouts: [
      'Broken reset behavior can accumulate stale upload payloads across frames.'
    ]
  }],
  ['StagingLinearAllocatorTests.AppendRawRejectsNullDataForNonZeroByteCount', {
    subsystem: 'Core Staging Allocator',
    level: 'White-box Unit',
    kind: 'Input Validation',
    description: 'Ensures invalid raw append requests fail loudly instead of producing undefined staging data.',
    content: 'Calls the raw append path with a non-zero byte count and a null pointer, then expects a runtime error.',
    validates: [
      'Null data pointers are rejected for non-empty uploads.',
      'The allocator does not silently accept invalid copies.',
      'Failure mode stays explicit for upstream callers.'
    ],
    watchouts: [
      'Silent acceptance here can produce hard-to-debug GPU-side corruption.'
    ]
  }],
  ['StagingLinearAllocatorTests.AppendRawWithZeroBytesIsANoOp', {
    subsystem: 'Core Staging Allocator',
    level: 'White-box Unit',
    kind: 'Edge Handling',
    description: 'Validates that zero-byte raw appends are treated as no-ops.',
    content: 'Seeds the arena with an existing payload, issues a zero-byte raw append, and checks that size and offset remain unchanged.',
    validates: [
      'Zero-byte operations do not mutate arena contents.',
      'Returned offsets reflect the pre-existing logical size.',
      'No extra padding or growth is introduced by an empty write.'
    ],
    watchouts: [
      'Empty writes appear frequently in batched upload code paths and must stay harmless.'
    ]
  }],
  ['HelperFunctionTests.WaveReadAcrossXReturnsInputUnchanged', {
    subsystem: 'Core Math Helpers',
    level: 'White-box Unit',
    kind: 'Scalar Identity',
    description: 'Confirms the current CPU-side stub for `WaveReadAcrossX` behaves as a pure identity function.',
    content: 'Passes integer and floating-point values through the helper and checks that both come back unchanged.',
    validates: [
      'The stubbed helper remains side-effect free.',
      'Integer inputs round-trip without mutation.',
      'Floating-point inputs round-trip without mutation.'
    ],
    watchouts: [
      'These helpers are often mirrored into shader DSL semantics, so accidental behavior drift matters.'
    ]
  }],
  ['HelperFunctionTests.WaveReadAcrossYReturnsInputUnchanged', {
    subsystem: 'Core Math Helpers',
    level: 'White-box Unit',
    kind: 'Scalar Identity',
    description: 'Confirms the current CPU-side stub for `WaveReadAcrossY` behaves as a pure identity function.',
    content: 'Passes integer and floating-point values through the helper and verifies the returned payload matches the input.',
    validates: [
      'The stubbed helper remains side-effect free.',
      'Signed integer inputs are preserved.',
      'Double-precision inputs remain unchanged.'
    ],
    watchouts: [
      'Even placeholder SIMD/wave helpers should keep deterministic semantics on the host side.'
    ]
  }],
  ['RhiSmokeTest.CreatesMappableBufferObject', {
    subsystem: 'Metal RHI',
    level: 'Black-box Integration',
    kind: 'Resource Smoke',
    description: 'Checks the minimal lifecycle for a CPU-visible buffer resource.',
    content: 'Creates a mapped buffer through the public RHI API and verifies the reported storage size.',
    validates: [
      'Device buffer creation succeeds on the active Metal backend.',
      'Requested storage size is surfaced correctly through the RHI handle.',
      'Explicit free path works for buffer resources.'
    ],
    watchouts: [
      'This is a cheap backend health check and should fail fast if buffer bootstrap regresses.'
    ]
  }],
  ['RhiSmokeTest.CreatesTextureAndSamplerObjects', {
    subsystem: 'Metal RHI',
    level: 'Black-box Integration',
    kind: 'Resource Smoke',
    description: 'Verifies texture and sampler creation and destruction on the Metal backend.',
    content: 'Creates a small 2D texture and a matching sampler, then frees both objects.',
    validates: [
      'Texture descriptor translation produces a valid Metal texture.',
      'Texture dimensions remain visible through the RHI interface.',
      'Sampler creation path stays operational.'
    ],
    watchouts: [
      'Descriptor translation errors often show up here before larger render passes fail.'
    ]
  }],
  ['RhiSmokeTest.CreatesShaderModuleAndComputePipeline', {
    subsystem: 'Metal RHI',
    level: 'Black-box Integration',
    kind: 'Pipeline Smoke',
    description: 'Compiles a minimal compute shader and materializes a compute pipeline.',
    content: 'Builds a shader module from inline MSL, creates a pipeline layout, and constructs a compute pipeline object.',
    validates: [
      'MSL compilation works through the public shader-module API.',
      'Pipeline-layout creation succeeds with an empty bind-group layout list.',
      'Compute pipeline state creation succeeds end-to-end.'
    ],
    watchouts: [
      'This catches backend compilation and pipeline bootstrap failures before runtime dispatch tests run.'
    ]
  }],
  ['RhiPipelineTest.CreatesDefaultAndCustomTextureViews', {
    subsystem: 'Metal RHI',
    level: 'Black-box Integration',
    kind: 'View Construction',
    description: 'Verifies that both default and explicit texture-view creation paths succeed on the Metal backend.',
    content: 'Creates a mipmapped 2D texture, obtains its implicit default view, then materializes an explicit custom view.',
    validates: [
      'Default view creation remains valid for ordinary textures.',
      'Explicit view descriptors translate into native Metal texture views.',
      'Texture lifetime owns and cleans up created views safely.'
    ],
    watchouts: [
      'View-creation bugs often surface later as bind-group failures and render-pass attachment mismatches.'
    ]
  }],
  ['RhiPipelineTest.CreatesMinimalRenderPipeline', {
    subsystem: 'Metal RHI',
    level: 'Black-box Integration',
    kind: 'Pipeline Smoke',
    description: 'Compiles a minimal vertex/fragment pair and materializes a render pipeline.',
    content: 'Builds inline MSL for a triangle pipeline, creates an empty pipeline layout, and constructs a render pipeline with one color target.',
    validates: [
      'Render-pipeline creation works independently of the sample applications.',
      'Inline MSL can supply both vertex and fragment stages.',
      'Color-target translation remains functional for simple pipelines.'
    ],
    watchouts: [
      'This broadens coverage beyond compute-only pipeline bootstrap.'
    ]
  }],
  ['RhiPipelineTest.CreatesRenderPipelineWithBlendStateEnabled', {
    subsystem: 'Metal RHI',
    level: 'Black-box Integration',
    kind: 'Pipeline State',
    description: 'Builds a render pipeline with blending enabled to cover a broader graphics state surface than the minimal smoke pipeline.',
    content: 'Creates a standard triangle render pipeline but enables explicit color and alpha blend factors on the render target.',
    validates: [
      'Blend-enabled render pipelines can be materialized through the public API.',
      'Color and alpha blend state translation remains functional.',
      'Graphics pipeline coverage extends beyond the minimal no-blend path.'
    ],
    watchouts: [
      'Blend state is a frequent source of backend translation drift across platforms.'
    ]
  }],
  ['RhiPipelineTest.CreatesBindGroupLayoutWithMixedBindings', {
    subsystem: 'Metal RHI',
    level: 'Black-box Integration',
    kind: 'Binding Layout',
    description: 'Builds a mixed buffer/texture/sampler bind-group layout through the public API.',
    content: 'Creates a bind-group layout that combines a uniform buffer, sampled texture, and filtering sampler entry.',
    validates: [
      'Mixed binding descriptors are accepted by the backend.',
      'Texture and sampler binding layouts coexist in one bind group.',
      'The public descriptor path can express a representative graphics binding contract.'
    ],
    watchouts: [
      'This is a common layout shape for real render pipelines, not just compute smoke cases.'
    ]
  }],
  ['RhiPipelineTest.CreatesPipelineLayoutWithMultipleBindGroupLayouts', {
    subsystem: 'Metal RHI',
    level: 'Black-box Integration',
    kind: 'Pipeline Layout Composition',
    description: 'Builds a pipeline layout from more than one bind-group layout to validate multi-set layout composition.',
    content: 'Creates separate storage-buffer and sampler bind-group layouts, then combines them into one pipeline layout through the public API.',
    validates: [
      'Multiple bind-group layouts can be composed into one pipeline layout.',
      'Storage and sampler layouts coexist in one higher-level pipeline contract.',
      'The public layout API handles non-trivial renderer binding organization.'
    ],
    watchouts: [
      'This adds useful coverage for real renderer layout composition without relying on fragile draw execution.'
    ]
  }],
  ['RhiExecutionTest.CreatesStorageBindGroupForComputeDispatch', {
    subsystem: 'Metal RHI',
    level: 'Black-box Integration',
    kind: 'Binding Contract',
    description: 'Builds the complete binding stack required for a storage-buffer-backed compute dispatch.',
    content: 'Creates a storage buffer, bind-group layout, bind group, pipeline layout, shader module, and compute pipeline through the public API.',
    validates: [
      'Storage-buffer bind-group descriptors are accepted by the Metal backend.',
      'Argument-buffer translation remains coherent for compute bindings.',
      'Pipeline layout creation and shader compilation agree on the binding contract.'
    ],
    watchouts: [
      'This is a prerequisite for treating GVM as a shader-backend runtime, even before full GPU readback coverage is added.'
    ]
  }],
  ['RhiExecutionTest.DispatchesComputePipelineCompletesWithoutGpuError', {
    subsystem: 'Metal RHI',
    level: 'Black-box Integration',
    kind: 'Compute Execution',
    description: 'Runs a real compute dispatch and waits for GPU completion using the Metal command-buffer bridge.',
    content: 'Creates storage-buffer-backed compute resources, encodes a compute pass, submits it through the public queue, and waits for command completion.',
    validates: [
      'Storage-buffer bind-group plumbing is functional for compute workloads.',
      'Compute dispatch reaches the backend without immediate GPU execution errors.',
      'Command-buffer completion waiting works for explicit user-encoded workloads.'
    ],
    watchouts: [
      'This intentionally stops short of asserting data readback because the current backend does not yet expose a stable end-to-end readback contract.'
    ]
  }],
  ['RhiExecutionTest.SubmitsBlitFillPassCompletesWithoutGpuError', {
    subsystem: 'Metal RHI',
    level: 'Black-box Integration',
    kind: 'Blit Execution',
    description: 'Exercises the explicit blit-pass path by filling a buffer and waiting for GPU completion.',
    content: 'Creates a copy-capable buffer, encodes a blit fill command, submits it, and waits on the backing Metal command buffer.',
    validates: [
      'Explicit blit-pass creation works through the public API.',
      'Fill-buffer commands can be submitted and completed successfully.',
      'The queue handles non-compute command workloads cleanly.'
    ],
    watchouts: [
      'This adds coverage for a backend path that is distinct from both render and compute execution.'
    ]
  }],
  ['RhiExecutionTest.RepeatedComputeDispatchBatchesComplete', {
    subsystem: 'Metal RHI',
    level: 'Black-box Integration',
    kind: 'Submission Stability',
    description: 'Runs multiple compute command-buffer submissions in sequence to catch command lifecycle regressions.',
    content: 'Reuses one compute pipeline and bind group across several separately submitted command encoders, waiting after each batch.',
    validates: [
      'Repeated command-buffer creation and submission remain stable.',
      'Compute pipelines and bind groups can be reused across batches.',
      'Completion waiting remains reliable across multiple submissions.'
    ],
    watchouts: [
      'Submission-lifecycle issues often surface only after more than one batch.'
    ]
  }],
  ['RhiExecutionTest.CopyBufferMultipleRegionRejectsNonAlignedRanges', {
    subsystem: 'Metal RHI',
    level: 'Black-box Integration',
    kind: 'Contract Validation',
    description: 'Asserts that multi-region buffer copies reject regions whose offsets or sizes are not 4-byte aligned.',
    content: 'Seeds a region descriptor with non-aligned byte offsets and size, invokes the public multi-region copy API, and expects the Metal backend to fail fast before dispatch.',
    requirements: [
      'The multi-region copy backend only accepts 4-byte-aligned srcOffset/dstOffset/size tuples.',
      'Alignment validation must happen at runtime on the real backend entry point.',
      'Non-aligned requests must fail explicitly instead of silently truncating the copied byte count.'
    ],
    validates: [
      'The backend exposes a clear contract instead of relying on undefined partial-copy behavior.',
      'Callers get an actionable failure before any GPU dispatch is encoded.',
      'Packed or tail-byte copies cannot silently corrupt data through truncation.'
    ],
    observability: [
      'A failing run shows up as an exception mismatch on the exact public API call.',
      'The raw artifact directory still keeps the target-level logs, but the HTML detail focuses on the violated alignment contract.'
    ],
    watchouts: [
      'Any caller that previously relied on byte-granular behavior now needs to align or split its copy regions explicitly.'
    ]
  }],
  ['RhiExecutionTest.RepeatedResourceCreationRoundTripDoesNotCrash', {
    subsystem: 'Metal RHI',
    level: 'Black-box Integration',
    kind: 'Resource Churn',
    description: 'Applies repeated create/free pressure to basic Metal resources to catch obvious lifecycle regressions.',
    content: 'Creates and destroys buffers, textures, and samplers in a loop through the public RHI API.',
    validates: [
      'Short-lived resource churn does not crash the backend.',
      'Core create/free paths remain stable under repetition.',
      'The test leaves the device in a usable state for subsequent cases.'
    ],
    watchouts: [
      'This is a first-step churn test, not a definitive leak detector.'
    ]
  }],
  ['SpdHiZReadbackTest.GeneratesMinAndMaxMipChainsMatchingCpuReference', {
    subsystem: 'Cross-platform RHI + DSL',
    level: 'Black-box Integration',
    kind: 'Texture Compute Readback Correctness',
    description: 'Validates an SPD-style HiZ compute path by comparing GPU min/max R16F mip chains against a CPU reference.',
    content: 'Uploads a deterministic Depth32Float texture, runs ordered DSL compute dispatches that write every R16Float HiZ mip through storage texture views, reads each mip back, and compares both min and max reductions texel-for-texel.',
    validates: [
      'Depth texture upload, sampled depth reads, R16Float storage texture writes, and texture readback work together end to end.',
      'Tail mip generation remains correct when the cross-workgroup reduction boundary is expressed as an ordered compute dispatch.',
      'Both min and max HiZ reduction modes preserve half-precision results exactly across all generated mip levels.'
    ],
    watchouts: [
      'The fixture intentionally uses a small power-of-two texture so the result isolates synchronization and readback correctness from NPOT mip extent policy.'
    ]
  }],
  ['GpuOneSweepSortTest.CompletionSignalPassWritesCpuVisibleFlag', {
    subsystem: 'Cross-platform RHI + DSL',
    level: 'Black-box Integration',
    kind: 'Headless GPU Completion Preflight',
    description: 'Verifies the smallest possible GPU-to-CPU visibility path: a DSL compute pass writes a completion flag into a storage buffer and the CPU polls it through the public mapping API.',
    content: 'Builds a headless DSL compute pass that stores `1` into a completion buffer, submits it through the public queue, and then polls the mapped buffer from the CPU side.',
    validates: [
      'The public queue can execute a DSL compute pass without any backend-private hooks.',
      'A mapped storage buffer is readable from the CPU after GPU execution.',
      'Later headless sort tests have a trustworthy completion mechanism.'
    ],
    watchouts: [
      'If this fails, treat completion visibility or mapped readback as broken before looking at the radix-sort algorithm.'
    ]
  }],
  ['GpuOneSweepSortTest.ComputeWritePatternIntoMappedStorageBufferMatchesCpuReference', {
    subsystem: 'Cross-platform RHI + DSL',
    level: 'Black-box Integration',
    kind: 'Headless GPU Write Preflight',
    description: 'Checks whether a DSL compute pass can write a deterministic pattern into a directly mapped storage buffer and have the CPU observe the exact contents.',
    content: 'Creates a mapped storage buffer, runs a DSL compute pass that writes predictable key/value pairs, then compares the mapped CPU-side contents against a CPU reference pattern.',
    validates: [
      'Storage-buffer writes from GPU shaders become visible through the public mapped-buffer API.',
      'The test isolates direct mapped-storage behavior without involving extra copy passes.',
      'The headless sort harness can trust mapped output buffers if this preflight passes.'
    ],
    watchouts: [
      'If this fails while the completion-only test also fails, the problem is likely in the mapped-buffer contract rather than the sort algorithm.'
    ]
  }],
  ['GpuOneSweepSortTest.ComputeWritePatternThenBlitReadbackMatchesCpuReference', {
    subsystem: 'Cross-platform RHI + DSL',
    level: 'Black-box Integration',
    kind: 'Headless GPU Readback Preflight',
    description: 'Checks the staged readback path by writing a deterministic pattern into a storage buffer, copying it into a mapped readback buffer, and comparing against a CPU reference.',
    content: 'Runs a DSL compute pass, blits the result into a readback buffer through the public blit API, and verifies the CPU-observed bytes against the expected pattern.',
    validates: [
      'The public compute + blit + mapped-readback chain works end to end.',
      'Sort tests can safely use a storage-only GPU result buffer plus a separate readback buffer.',
      'Failures can be localized to readback staging rather than one-sweep sorting.'
    ],
    watchouts: [
      'This is the staging-path prerequisite for the standalone sort benchmark and correctness tests.'
    ]
  }],
  ['GpuOneSweepSortTest.SortsSmallFixedSequenceAgainstCpuReference', {
    subsystem: 'Cross-platform RHI + DSL',
    level: 'Black-box Integration',
    kind: 'Headless One-Sweep Sort Sanity',
    description: 'Sorts a small fixed sequence with duplicate keys to validate stable ordering before scaling up to large randomized workloads.',
    content: 'Uploads a handcrafted key/value sequence, runs the DSL one-sweep radix-sort pipeline headlessly, reads the final result back through the public API, and compares it against a CPU stable sort.',
    validates: [
      'The one-sweep pipeline preserves stable ordering for duplicate keys.',
      'Small deterministic regressions can be caught without large benchmark runs.',
      'The headless sort harness is wired correctly before multi-partition stress tests.'
    ],
    watchouts: [
      'If this fails, the radix algorithm or its surrounding pass wiring is broken even before performance enters the picture.'
    ]
  }],
  ['GpuOneSweepSortTest.SortsRandomMultiPartitionSequenceAgainstCpuReference', {
    subsystem: 'Cross-platform RHI + DSL',
    level: 'Black-box Integration',
    kind: 'Headless One-Sweep Sort Correctness',
    description: 'Generates a large duplicate-rich key/value sequence at runtime, sorts it on the GPU with a standalone one-sweep radix path, and compares the full stable result against a CPU reference sort.',
    content: 'Allocates storage buffers for a long deterministic sequence, runs a headless one-sweep radix sort without creating a window or swapchain, copies the final result into a mapped readback buffer, and checks every key/value pair against `std::stable_sort` output.',
    validates: [
      'The standalone GPU one-sweep radix-sort implementation produces a fully correct stable global ordering.',
      'The public RHI + DSL compute path can execute a large headless storage-buffer workload end to end.',
      'The staged readback path is stable enough to use this case as a regression gate before wiring one-sweep sort changes into samples.'
    ],
    watchouts: [
      'If this fails after all preflight cases pass, treat it as a sorting-core regression first, not a renderer bug or sample bug.'
    ]
  }],
  ['GpuOneSweepSortTest.BenchmarksVeryLargeRandomSequenceAndMatchesCpuReference', {
    subsystem: 'Cross-platform RHI + DSL',
    level: 'Black-box Integration',
    kind: 'Headless One-Sweep Sort Benchmark',
    description: 'Measures the standalone GPU one-sweep radix-sort path on a very large sequence while still enforcing full correctness against a CPU reference.',
    content: 'Runs several headless GPU one-sweep sort iterations over a long deterministic sequence, records average GPU timing and throughput, then validates the final output against CPU sorting.',
    validates: [
      'Performance changes in the one-sweep sort core can be measured outside the renderer.',
      'Correctness remains enforced even for the benchmark-sized workload.',
      'The project has a dedicated place to iterate toward higher-end GPU sorting strategies before sample integration.'
    ],
    watchouts: [
      'Treat benchmark deltas together with correctness results; a faster but unstable sort is not acceptable.'
    ]
  }],
  ['DslCodegenSmokeTests.GeneratesExpectedArtifacts', {
    subsystem: 'UGLC / Unified C++ DSL',
    level: 'Black-box Integration',
    kind: 'Codegen Smoke',
    description: 'Confirms the DSL compiler emits the expected generated header and stamp artifacts for the triangle sample.',
    content: 'Reads the generated output directory, checks required files exist, and validates key pipeline symbols in the generated header.',
    validates: [
      'UGLC runs from CMake-driven test execution.',
      'Expected generated files are produced for the sample DSL source.',
      'The generated output contains the core render-pipeline symbols.'
    ],
    watchouts: [
      'This is the lowest-cost end-to-end check for the DSL backend entry point.'
    ]
  }],
  ['DslCodegenSmokeTests.EmitsRendererLifecycleAndBindGroupHooks', {
    subsystem: 'UGLC / Unified C++ DSL',
    level: 'Black-box Integration',
    kind: 'Codegen Contract',
    description: 'Checks that the generated triangle renderer still emits expected lifecycle and bind-group setup helpers.',
    content: 'Inspects the generated header for pipeline initialization hooks, bind-group setup helpers, and render-set wiring symbols.',
    validates: [
      'Renderer lifecycle scaffolding is emitted into generated code.',
      'Bind-group layout creation remains visible in the output.',
      'Generated runtime glue still resembles an executable renderer contract.'
    ],
    watchouts: [
      'This protects the ergonomic runtime layer around the raw shader/backend codegen.'
    ]
  }],
  ['DslStorageCodegenTests.EmitsStorageBufferBindingContracts', {
    subsystem: 'UGLC / Unified C++ DSL',
    level: 'Black-box Integration',
    kind: 'Codegen Contract',
    description: 'Validates that DSL codegen preserves storage-buffer bind-group contracts for the buffer-backed triangle sample.',
    content: 'Inspects generated code for bind-group layout declarations, storage buffer access qualifiers, and bind-group construction calls.',
    validates: [
      'Storage-buffer bindings are emitted into the generated header.',
      'Read-write access semantics survive code generation.',
      'Generated bind-group and layout construction code is present.'
    ],
    watchouts: [
      'This is directly tied to using GVM as a unified C++ shader backend.'
    ]
  }],
  ['DslStorageCodegenTests.EmitsStorageBufferStageVisibility', {
    subsystem: 'UGLC / Unified C++ DSL',
    level: 'Black-box Integration',
    kind: 'Codegen Contract',
    description: 'Checks that generated storage-buffer bindings still carry the expected shader-stage visibility and pipeline references.',
    content: 'Searches the generated header for stage flags, storage buffer binding types, and the render pipeline symbol tied to the sample.',
    validates: [
      'Stage visibility flags survive code generation.',
      'Storage buffer binding types remain explicit in generated code.',
      'Pipeline symbols continue to reference the generated buffer-backed renderer.'
    ],
    watchouts: [
      'Visibility drift here would break shader/backend ABI expectations.'
    ]
  }],
  ['DslTextureCodegenTests.EmitsTextureAndSamplerBindingContracts', {
    subsystem: 'UGLC / Unified C++ DSL',
    level: 'Black-box Integration',
    kind: 'Codegen Contract',
    description: 'Validates texture and sampler binding emission for the textured triangle sample.',
    content: 'Checks the generated header for texture sampling declarations, sampler binding types, and bind-group wiring.',
    validates: [
      'Texture sample types are emitted correctly.',
      'Sampler binding modes are preserved by the DSL backend.',
      'Generated bind-group code wires texture views and samplers together.'
    ],
    watchouts: [
      'This guards the most common resource-binding path for shader-facing DSL output.'
    ]
  }],
  ['DslTextureCodegenTests.EmitsTextureSamplingPipelineContracts', {
    subsystem: 'UGLC / Unified C++ DSL',
    level: 'Black-box Integration',
    kind: 'Codegen Contract',
    description: 'Checks that generated textured pipelines still expose texture sampling and fragment-stage pipeline contracts.',
    content: 'Searches the generated header for explicit sample-type declarations, filtering sampler modes, fragment shader symbols, and pipeline creation code.',
    validates: [
      'Texture sampling types remain explicit in generated code.',
      'Filtering sampler contracts survive code generation.',
      'Fragment-stage pipeline scaffolding remains present.'
    ],
    watchouts: [
      'This protects one of the most important unified C++ shader backend paths.'
    ]
  }],
  ['DslCubeCodegenTests.EmitsMultiPassRendererContracts', {
    subsystem: 'UGLC / Unified C++ DSL',
    level: 'Black-box Integration',
    kind: 'Codegen Contract',
    description: 'Validates that the cube renderer sample emits the expected multi-pass render and compute runtime contracts.',
    content: 'Inspects generated code for the cube sample and checks for render-class, compute-class, and texture-construction symbols.',
    validates: [
      'Multi-pass renderer samples continue to compile through the DSL backend.',
      'Generated output contains both render and compute pipeline construction code.',
      'Texture allocation and multi-stage renderer wiring are preserved in code generation.'
    ],
    watchouts: [
      'This expands coverage from toy triangle samples into a more representative renderer composition case.'
    ]
  }],
  ['DslCubeCodegenTests.EmitsIntermediateTexturesAndPerPassClasses', {
    subsystem: 'UGLC / Unified C++ DSL',
    level: 'Black-box Integration',
    kind: 'Codegen Contract',
    description: 'Checks that the cube sample still emits intermediate resources and pass-specific runtime classes.',
    content: 'Inspects generated code for intermediate texture symbols, composite bind groups, and per-pass renderer classes used by the cube sample.',
    validates: [
      'Intermediate render resources remain explicit in generated code.',
      'Per-pass class generation is preserved for composed renderers.',
      'Composite-stage resource wiring remains visible in the emitted runtime layer.'
    ],
    watchouts: [
      'This keeps pressure on the DSL backend to support multi-stage renderer composition, not just simple shader stubs.'
    ]
  }],
  ['DslCubeCodegenTests.UsesComputeVisibilityForCheckerboardStorageTextures', {
    subsystem: 'UGLC / Unified C++ DSL',
    level: 'Black-box Integration',
    kind: 'Known Bug Exposure',
    description: 'Checks that the generated cube-scene storage textures are marked visible to compute stages rather than fragment stages only.',
    content: 'Slices the generated cube header around the checkerboard storage bind-group layout and asserts both storage-texture entries carry `ShaderStage::Compute` visibility.',
    requirements: [
      'Storage textures used by `CheckerBoardBackground` and `Composite` must be visible from compute shaders.',
      'The assertion must target the concrete generated bind-group layout, not a generic string somewhere else in the file.',
      'This case stays red until stage-visibility inference is fixed in code generation.'
    ],
    validates: [
      'Generated bind-group layouts reflect the stage that actually consumes the resource.',
      'The bug is caught at codegen time before it becomes a harder-to-debug runtime pipeline failure.',
      'The cube sample remains a regression canary for unified C++ DSL shader visibility contracts.'
    ],
    observability: [
      'The failure shows the missing compute-stage string directly in the generated-header assertion.',
      'The generated header is copied into the run artifacts for manual inspection.'
    ],
    watchouts: [
      'Wrong stage visibility can break backend portability even if one backend accidentally tolerates the mismatch.'
    ]
  }],
  ['DslCubeCodegenTests.CameraBindGroupVisibilityCoversVertexAndComputeUsage', {
    subsystem: 'UGLC / Unified C++ DSL',
    level: 'Black-box Integration',
    kind: 'Known Bug Exposure',
    description: 'Checks that the generated `CameraBindGroup` visibility covers the shader stages that actually consume it, instead of fragment-only visibility.',
    content: 'Slices the generated cube header around `CameraBindGroupBindGroupLayout` and asserts the emitted visibility still references both vertex-stage and compute-stage usage for the camera uniform buffer.',
    requirements: [
      'The camera uniform bind group must reflect both render-path vertex usage and compute-path usage from the cube sample.',
      'The assertion must inspect the concrete generated bind-group layout for `CameraBindGroup`, not unrelated strings elsewhere in the file.',
      'This case stays red until stage-usage inference becomes precise enough for strict backends.'
    ],
    validates: [
      'Generated visibility metadata matches actual shader consumption across multiple stages.',
      'The bug is caught at codegen time before stricter APIs reject pipeline layout creation.',
      'The cube sample continues to serve as a regression canary for multi-stage uniform bindings.'
    ],
    observability: [
      'The failure pinpoints the missing stage tokens directly in the generated-header assertion window.',
      'The generated header is copied into the run artifacts so the exact emitted layout can be audited.'
    ],
    watchouts: [
      'Fragment-only visibility may appear harmless on permissive backends, but it is a portability defect for WebGPU/Vulkan-class validation.'
    ]
  }],
  ['DslRenderFeatureTests.PresentsProceduralTriangleToWindow', {
    subsystem: 'DSL Render Feature Host',
    level: 'Windowed Black-box Integration',
    kind: 'Desktop Presentation',
    description: 'Runs the single-host desktop render executable in procedural-triangle mode and presents several frames to a real swapchain-backed window.',
    content: 'The shared render host keeps one SDL + Metal window alive, switches the generated renderer to mode 0, and presents a short burst of frames from a DSL-defined procedural triangle pass.',
    requirements: [
      'The case must execute inside the shared render-host executable instead of a dedicated one-off sample app.',
      'A visible desktop window and swapchain-backed present path must be exercised.',
      'At least several frames should be presented so window creation and repeated present are both covered.'
    ],
    validates: [
      'Single-host render-case dispatch works.',
      'Procedural vertex generation, render-pipeline creation, and present all survive a real windowed path.',
      'The artifact manifest records the mode, frame count, and window geometry for later auditing.'
    ],
    observability: [
      'The HTML detail explains the case contract; the artifact directory contains a compact JSON manifest for this windowed run.',
      'A local user can visually confirm the window appears and updates during execution.'
    ],
    platformStrategy: [
      'This case is intentionally hosted in one executable so the same architecture can later become a single iOS or Android test app with multiple scenes.'
    ],
    watchouts: [
      'Do not split this back into separate binaries unless there is a strong platform-specific reason; the single-host shape is part of the portability plan.'
    ]
  }],
  ['DslRenderFeatureTests.PresentsIndexedTriangleWithVertexBuffers', {
    subsystem: 'DSL Render Feature Host',
    level: 'Windowed Black-box Integration',
    kind: 'Vertex and Index Input',
    description: 'Presents the buffered-triangle case through the shared render host to exercise vertex-buffer and index-buffer bindings in a real window.',
    content: 'The render host switches to mode 1, binds explicit vertex and index buffers generated from DSL-facing buffer types, and presents several frames to the swapchain.',
    requirements: [
      'The case must use explicit vertex and index buffers rather than procedural vertex IDs only.',
      'Presentation still happens through the same single host executable and window.',
      'The case should cover repeated frames, not only one-shot pipeline creation.'
    ],
    validates: [
      'Vertex-input layout and index-buffer binding survive the generated runtime path.',
      'Host-side buffer uploads and DSL-side draw wiring agree on layout.',
      'Desktop presentation remains stable after switching render modes inside one process.'
    ],
    observability: [
      'A per-case artifact manifest captures the active mode and frame count.',
      'Failures surface either as a test assertion, a process error, or a visible present-path problem during local runs.'
    ],
    platformStrategy: [
      'Keeping this in the shared host makes future mobile remote execution a scene switch instead of another packaged app.'
    ],
    watchouts: [
      'This case is a practical regression trap for vertex-layout ABI mismatches between DSL output and runtime binding code.'
    ]
  }],
  ['DslRenderFeatureTests.PresentsTexturedTriangleWithSamplerBindings', {
    subsystem: 'DSL Render Feature Host',
    level: 'Windowed Black-box Integration',
    kind: 'Texture Sampling',
    description: 'Runs the textured-triangle mode in the shared render host to cover sampled textures, sampler state, and bind-group wiring under presentation.',
    content: 'The host switches to mode 2, binds a small uploaded texture plus a sampler through a DSL-generated bind group, and presents several textured frames to the window.',
    requirements: [
      'The case must cover sampled texture and sampler bindings through generated DSL runtime code.',
      'Texture data upload and present must both happen in the same run.',
      'The window path remains shared with the other render modes.'
    ],
    validates: [
      'Texture creation, upload, bind-group creation, and fragment sampling work together.',
      'Mode switching inside the shared renderer does not invalidate previously created GPU objects.',
      'The render host remains suitable for a future multi-scene mobile test app.'
    ],
    observability: [
      'The case writes a compact artifact manifest and appears as its own report row with detailed contract text.',
      'The local desktop run also gives immediate visual confirmation that a textured scene is being presented.'
    ],
    platformStrategy: [
      'This scene-oriented setup is the same structure a mobile host app will need when it cycles through texture-feature cases in one launch.'
    ],
    watchouts: [
      'Texture/sampler ABI drift is easy to miss in code review; this case keeps it exercised end to end.'
    ]
  }],
  ['DslRenderFeatureTests.PresentsMultiPassCubeCompositeScene', {
    subsystem: 'DSL Render Feature Host',
    level: 'Windowed Black-box Integration',
    kind: 'Multi-pass Render and Compute',
    description: 'Runs the most complex desktop render mode: compute-generated background, cube g-buffer pass, composite compute pass, and final present quad in one host executable.',
    content: 'The host switches to mode 3, updates camera data each frame, runs the checkerboard compute pass, renders the cube into intermediate targets, composites into a storage texture, and finally presents the result through a fullscreen quad.',
    requirements: [
      'One executable and one window must cover the full multi-pass scene rather than delegating to a separate sample application.',
      'The case must exercise both compute and render classes plus intermediate textures before present.',
      'Several frames should be rendered so uniform updates and repeated submission are both covered.'
    ],
    validates: [
      'The unified DSL backend can drive a materially more complex scene than a triangle sample.',
      'Intermediate textures, camera-buffer updates, compute passes, render passes, and final presentation work together in one runtime.',
      'The shared-host architecture is viable for the future mobile “one app runs all scenes” requirement.'
    ],
    observability: [
      'The report row documents the case contract in detail and the artifact manifest records that the multi-pass mode was executed.',
      'A local run also provides direct visual evidence that the popup window presented the complex scene.'
    ],
    platformStrategy: [
      'This is the desktop prototype for the eventual mobile remote runner model: one host app, many scenes, one aggregated report.'
    ],
    watchouts: [
      'This case is intentionally the canary for regressions in render/compute interop, storage-texture binding, and swapchain presentation.'
    ]
  }],
  ['DslRenderFeatureTests.PresentsInstancedTriangleSwarmScene', {
    subsystem: 'DSL Render Feature Host',
    level: 'Windowed Black-box Integration',
    kind: 'Instancing',
    description: 'Presents a dense grid of procedurally generated instanced triangles to exercise `InstanceID`-driven vertex generation in the unified DSL backend.',
    content: 'The shared host switches to mode 4 and renders multiple small triangles from one draw call, with position and color derived from the instance index rather than external assets.',
    requirements: [
      'The scene must use one draw call with multiple instances instead of duplicating geometry on the CPU.',
      'No external meshes or textures are allowed; all positions and colors are generated procedurally in DSL code.',
      'Presentation still happens through the same long-lived SDL window and swapchain.'
    ],
    validates: [
      'InstanceID plumbing from generated shader code through runtime submission remains correct.',
      'The renderer can present a denser scene than a single triangle without introducing host-side asset loading.',
      'This case broadens parity with common WebGPU instancing samples.'
    ],
    observability: [
      'The artifact manifest records the new render mode and frame count, and the local window should show a visible triangle swarm.'
    ],
    platformStrategy: [
      'This remains a single scene inside the unified host executable, keeping the same one-app execution model planned for mobile.'
    ],
    watchouts: [
      'Instancing regressions are easy to miss if the backend silently falls back to per-draw duplication.'
    ]
  }],
  ['DslRenderFeatureTests.PresentsFullscreenProceduralGradientScene', {
    subsystem: 'DSL Render Feature Host',
    level: 'Windowed Black-box Integration',
    kind: 'Fullscreen Procedural Shading',
    description: 'Presents a fullscreen procedural gradient scene generated entirely in shader code, with no vertex buffers and no external textures.',
    content: 'The host switches to mode 5, draws a fullscreen triangle, and shades the output with analytic gradients, rings, and vignette terms in the fragment shader.',
    requirements: [
      'The scene must avoid external assets and rely purely on procedural shading.',
      'The case should cover fullscreen triangle generation and fragment-heavy shading logic.',
      'It must still render through the same shared host executable and window.'
    ],
    validates: [
      'Fullscreen procedural shading paths remain stable under the unified DSL backend.',
      'The renderer can cover postprocess-style fragment workloads without auxiliary textures.',
      'This is a useful analog to WebGPU fullscreen shader demos and gradient samples.'
    ],
    observability: [
      'The popup window should show a fullscreen color field rather than geometry-dependent content, making visual validation immediate.'
    ],
    platformStrategy: [
      'Procedural fullscreen cases are especially suitable for future mobile smoke runs because they need no assets and little CPU setup.'
    ],
    watchouts: [
      'This case is a good canary for fullscreen-triangle conventions, UV generation, and fragment-output format handling.'
    ]
  }],
  ['DslRenderFeatureTests.PresentsComputeGeneratedPatternScene', {
    subsystem: 'DSL Render Feature Host',
    level: 'Windowed Black-box Integration',
    kind: 'Compute-to-Present',
    description: 'Runs a compute-generated pattern scene and presents it through a sampled fullscreen quad in the same host executable.',
    content: 'The host switches to mode 6, fills a storage texture from a compute pass, then samples that texture through a present quad without relying on external images.',
    requirements: [
      'A compute pass must write the source image for presentation.',
      'The generated texture must then be sampled in a render pass before swapchain present.',
      'No external texture assets are allowed.'
    ],
    validates: [
      'Storage-texture writes, sampled-texture reads, and presentation interoperate correctly in one frame graph.',
      'The DSL backend can express common WebGPU-style compute texture demos without external assets.',
      'The shared host architecture supports compute-heavy cases as first-class scenes.'
    ],
    observability: [
      'The report row identifies this as a compute-to-present path, and the local window should show a patterned image rather than triangle geometry.'
    ],
    platformStrategy: [
      'This scene shape maps directly to a mobile one-app runner where compute and render cases must coexist in a single process.'
    ],
    watchouts: [
      'This is a high-value regression trap for storage-texture usage flags, bind-group layout emission, and cross-pass resource visibility.'
    ]
  }],
  ['DslRenderFeatureTests.PresentsOffscreenPostProcessScene', {
    subsystem: 'DSL Render Feature Host',
    level: 'Windowed Black-box Integration',
    kind: 'Render-to-Texture Postprocess',
    description: 'Renders a scene into an offscreen target and then presents it through a postprocess fullscreen pass in the same frame.',
    content: 'The host switches to mode 7, renders instanced geometry into an intermediate texture, then samples that texture through a separate postprocess pass before swapchain present.',
    requirements: [
      'At least two render passes must execute: offscreen scene generation and fullscreen presentation.',
      'The intermediate texture must be both a render attachment and a sampled texture.',
      'The case must remain asset-free and live inside the shared windowed host.'
    ],
    validates: [
      'Offscreen render targets, texture sampling, and postprocess presentation work together under the DSL runtime.',
      'The engine can express a common WebGPU sample pattern: render-to-texture followed by fullscreen composition.',
      'This increases coverage of cross-pass texture lifetime and attachment-state transitions.'
    ],
    observability: [
      'The local window should clearly show a processed scene rather than a direct scene render, and the artifact manifest records the dedicated mode.'
    ],
    platformStrategy: [
      'A single-host postprocess scene is directly reusable in a future mobile runner where many scenes must be cycled without repackaging apps.'
    ],
    watchouts: [
      'This case is designed to catch regressions in offscreen attachment creation, sampled presentation, and multi-pass command sequencing.'
    ]
  }]
]);

export function describeTestCase(testCase) {
  if (catalog.has(testCase.fullName)) {
    return catalog.get(testCase.fullName);
  }

  return {
    subsystem: 'Unclassified',
    level: 'Unknown',
    kind: 'General',
    description: 'No explicit metadata has been registered for this test yet.',
    content: 'Review the source file and raw JSON artifact for case-specific intent.',
    requirements: [
      'Capture this case in the catalog if it should appear as a first-class documented regression contract.'
    ],
    validates: [
      'Case executed through the standard GVM GoogleTest pipeline.'
    ],
    observability: [
      'Use the generated summary JSON and per-case artifact files for deeper inspection.'
    ],
    watchouts: [
      'Add a catalog entry to enrich report details for this test.'
    ]
  };
}
