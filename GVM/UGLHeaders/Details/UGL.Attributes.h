#ifndef UGL_ATTR_INDEXIBUTES_HPP
#define UGL_ATTR_INDEXIBUTES_HPP

// ---------- 工具宏 ----------
#define UGL_STR_ARGS_IMPL(...) #__VA_ARGS__              // 直接字符串化
#define UGL_STR_ARGS(...) UGL_STR_ARGS_IMPL(__VA_ARGS__) // 二次展开，保证 __VA_ARGS__ 先被展开

/*
 * ---------- 主要宏 ----------
 * 对于：
 *   UGL_ATTR_INDEX_FUNC()               → "UGL_ATTR_INDEX_FUNC"
 *   UGL_ATTR_INDEX_FUNC(0,1,2,3)        → "UGL_ATTR_INDEX_FUNC0,1,2,3"
 *   UGL_ATTR_INDEX_FUNC(foo, bar, baz)  → "UGL_ATTR_INDEX_FUNCfoo,bar,baz"
 */
#define UGL_ATTR_INDEX_FUNC(name, ...) clang::annotate(__VA_OPT__(UGL_STR_ARGS(name(__VA_ARGS__))))


// ---------- 基础工具宏 ----------
#define UGL_CAT_(a, b) a##b          // 第 1 层：真正做 token pasting
#define UGL_CAT(a, b) UGL_CAT_(a, b) // 第 2 层：让参数先被展开，再进入 CAT_

#define UGL_STR_(x) #x         // 第 1 层：真正做 stringification
#define UGL_STR(x) UGL_STR_(x) // 第 2 层：让参数先被展开，再进入 STR_

// ---------- 你的需求宏 ----------
#define UGL_ATTR_INDEX(x, n) clang::annotate(UGL_STR(UGL_CAT(x, n)))
#define UGL_ATTR(x) clang::annotate(#x)


#define Slot0 UGL_ATTR_INDEX(Slot, 0)
#define Slot1 UGL_ATTR_INDEX(Slot, 1)
#define Slot2 UGL_ATTR_INDEX(Slot, 2)
#define Slot3 UGL_ATTR_INDEX(Slot, 3)
#define Slot4 UGL_ATTR_INDEX(Slot, 4)
#define Slot5 UGL_ATTR_INDEX(Slot, 5)
#define Slot6 UGL_ATTR_INDEX(Slot, 6)
#define Slot7 UGL_ATTR_INDEX(Slot, 7)

#define VertexInput0 UGL_ATTR_INDEX(VertexInput, 0)


#define Binding0 UGL_ATTR_INDEX(Binding, 0)
#define Binding1 UGL_ATTR_INDEX(Binding, 1)
#define Binding2 UGL_ATTR_INDEX(Binding, 2)
#define Binding3 UGL_ATTR_INDEX(Binding, 3)
#define Binding4 UGL_ATTR_INDEX(Binding, 4)
#define Binding5 UGL_ATTR_INDEX(Binding, 5)
#define Binding6 UGL_ATTR_INDEX(Binding, 6)
#define Binding7 UGL_ATTR_INDEX(Binding, 7)
#define Binding8 UGL_ATTR_INDEX(Binding, 8)
#define Binding9 UGL_ATTR_INDEX(Binding, 9)
#define Binding10 UGL_ATTR_INDEX(Binding, 10)
#define Binding11 UGL_ATTR_INDEX(Binding, 11)
#define Binding12 UGL_ATTR_INDEX(Binding, 12)
#define Binding13 UGL_ATTR_INDEX(Binding, 13)
#define Binding14 UGL_ATTR_INDEX(Binding, 14)
#define Binding15 UGL_ATTR_INDEX(Binding, 15)
#define Binding16 UGL_ATTR_INDEX(Binding, 16)
#define Binding17 UGL_ATTR_INDEX(Binding, 17)
#define Binding18 UGL_ATTR_INDEX(Binding, 18)
#define Binding19 UGL_ATTR_INDEX(Binding, 19)
#define Binding20 UGL_ATTR_INDEX(Binding, 20)
#define Binding21 UGL_ATTR_INDEX(Binding, 21)
#define Binding22 UGL_ATTR_INDEX(Binding, 22)
#define Binding23 UGL_ATTR_INDEX(Binding, 23)
#define Binding24 UGL_ATTR_INDEX(Binding, 24)
#define Binding25 UGL_ATTR_INDEX(Binding, 25)
#define Binding26 UGL_ATTR_INDEX(Binding, 26)
#define Binding27 UGL_ATTR_INDEX(Binding, 27)
#define Binding28 UGL_ATTR_INDEX(Binding, 28)
#define Binding29 UGL_ATTR_INDEX(Binding, 29)
#define Binding30 UGL_ATTR_INDEX(Binding, 30)
#define Binding31 UGL_ATTR_INDEX(Binding, 31)

#define Position UGL_ATTR(Position)


#define VertexID UGL_ATTR(VertexID)
#define TessFactor UGL_ATTR(TessFactor)
#define InsideTessFactor UGL_ATTR(InsideTessFactor)
#define PrimitiveID UGL_ATTR(PrimitiveID)
#define OutputControlPointID UGL_ATTR(OutputControlPointID)
#define DomainLocation UGL_ATTR(DomainLocation)
#define OutputControlPointID UGL_ATTR(OutputControlPointID)
#define TessDomain(x) UGL_ATTR_INDEX_FUNC(TessDomain, x)
#define TessDomainTriangle UGL_STR_ARGS(TessDomainTriangle)
#define TessPartition(x) UGL_ATTR_INDEX_FUNC(TessPartition, x)
#define TessPartitionInteger UGL_STR_ARGS(TessPartitionInteger)
#define OutputTopology(x) UGL_ATTR_INDEX_FUNC(OutputTopology, x)
#define OutputTopologyTriangleCW UGL_STR_ARGS(OutputTopologyTriangleCW)
#define OutputControlPoints(x) UGL_ATTR_INDEX_FUNC(OutputControlPoints, x)
#define MaxTessFactor(x) UGL_ATTR_INDEX_FUNC(MaxTessFactor, x)


#define Attribute0 UGL_ATTR_INDEX(Attribute, 0)
#define Attribute1 UGL_ATTR_INDEX(Attribute, 1)
#define Attribute2 UGL_ATTR_INDEX(Attribute, 2)
#define Attribute3 UGL_ATTR_INDEX(Attribute, 3)
#define Attribute4 UGL_ATTR_INDEX(Attribute, 4)
#define Attribute5 UGL_ATTR_INDEX(Attribute, 5)
#define Attribute6 UGL_ATTR_INDEX(Attribute, 6)
#define Attribute7 UGL_ATTR_INDEX(Attribute, 7)


#define Export UGL_ATTR(Export)


#define DispatchThreadID UGL_ATTR(DispatchThreadID)


#define GroupThreadID UGL_ATTR(GroupThreadID)
#define GroupID UGL_ATTR(GroupID)
#define GroupIndex UGL_ATTR(GroupIndex)

#define IN [[UGL_ATTR(IN)]]


#define OUT [[UGL_ATTR(OUT)]]


#define INOUT [[UGL_ATTR(INOUT)]]

#define LocalWorkGroupSize(x, y, z) UGL_ATTR_INDEX_FUNC(LocalWorkGroupSize, x, y, z)

#define InstanceID UGL_ATTR(InstanceID)


#define PrimitiveID UGL_ATTR(PrimitiveID)


#define RenderEntityInstanceID UGL_ATTR(RenderEntityInstanceID)


#define RenderEntityID UGL_ATTR(RenderEntityID)


#define RenderSetVertexBuffer UGL_ATTR(RenderSetVertexBuffer)


#define RenderSetIndexBuffer UGL_ATTR(RenderSetIndexBuffer)

#define Barycentrics UGL_ATTR(Barycentrics)
#define WaveLaneIndex UGL_ATTR(WaveLaneIndex)
#define WaveLaneCount UGL_ATTR(WaveLaneCount)
#define PixelLocalInput UGL_ATTR(PixelLocalInput)
#define PixelCoord UGL_ATTR(PixelCoord)
#define SampleIndex UGL_ATTR(SampleIndex)


#endif // UGL_ATTR_INDEXIBUTES_HPP
