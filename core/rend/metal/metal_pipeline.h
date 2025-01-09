/*
    Copyright 2024 flyinghead

    This file is part of Flycast.

    Flycast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Flycast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Flycast.  If not, see <https://www.gnu.org/licenses/>.
*/
#pragma once
#include "types.h"
#include <Metal/Metal.hpp>
#include <map>

#include "cfg/option.h"
#include "hw/pvr/ta_ctx.h"

class MetalRenderer;

enum class ModVolMode { Xor, Or, Inclusion, Exclusion, Final };

class MetalPipelineManager
{
public:
    explicit MetalPipelineManager(MetalRenderer *renderer);
    virtual ~MetalPipelineManager() = default;

    void term()
    {
        pipelines.clear();
        depthPassPipelines.clear();
        depthStencilStates.clear();
        depthPassDepthStencilStates.clear();
    }

    MTL::RenderPipelineState* GetBlitPassPipeline() {
        if (blitPassPipeline != nullptr)
            return blitPassPipeline;

        CreateBlitPassPipeline();

        return blitPassPipeline;
    }

    MTL::RenderPipelineState* GetDepthPassPipeline(int cullMode, bool naomi2)
    {
        u32 pipehash = hash(cullMode, naomi2);
        const auto &pipeline = depthPassPipelines.find(pipehash);
        if (pipeline != depthPassPipelines.end() && pipeline->second != nullptr)
            return pipeline->second;
        CreateDepthPassPipeline(cullMode, naomi2);

        return depthPassPipelines[pipehash];
    }

    MTL::RenderPipelineState* GetModifierVolumePipeline(ModVolMode mode, int cullMode, bool naomi2)
    {
        u32 pipehash = hash(mode, cullMode, naomi2);
        const auto &pipeline = modVolPipelines.find(pipehash);
        if (pipeline != modVolPipelines.end() && pipeline->second != nullptr)
            return pipeline->second;
        CreateModVolPipeline(mode, cullMode, naomi2);

        return modVolPipelines[pipehash];
    }

    MTL::RenderPipelineState* GetPipeline(u32 listType, bool sortTriangles, const PolyParam& pp, int gpuPalette, bool dithering)
    {
        u64 pipehash = hash(listType, sortTriangles, &pp, gpuPalette, dithering);
        const auto &pipeline = pipelines.find(pipehash);
        if (pipeline != pipelines.end() && pipeline->second != nullptr)
            return pipeline->second;
        CreatePipeline(listType, sortTriangles, pp, gpuPalette, dithering);

        return pipelines[pipehash];
    }

    MTL::DepthStencilState* GetModVolDepthStencilStates(ModVolMode mode, int cullMode, bool naomi2)
    {
        u32 pipehash = hash(mode, cullMode, naomi2);
        const auto &state = modVolStencilStates.find(pipehash);
        if (state != modVolStencilStates.end() && state->second != nullptr)
            return state->second;
        CreateModVolDepthStencilState(mode, cullMode, naomi2);

        return modVolStencilStates[pipehash];
    }

    MTL::DepthStencilState* GetDepthPassDepthStencilStates(int cullMode, bool naomi2)
    {
        u32 pipehash = hash(cullMode, naomi2);
        const auto &state = depthPassDepthStencilStates.find(pipehash);
        if (state != depthPassDepthStencilStates.end() && state->second != nullptr)
            return state->second;
        CreateDepthPassDepthStencilState(cullMode, naomi2);

        return depthPassDepthStencilStates[pipehash];
    }

    MTL::DepthStencilState* GetDepthStencilStates(u32 listType, bool sortTriangles, bool shadowed, const PolyParam& pp)
    {
        u64 pipehash = hash(listType, sortTriangles, shadowed, &pp);

        const auto &state = depthStencilStates.find(pipehash);
        if (state != depthStencilStates.end() && state->second != nullptr)
            return state->second;
        CreateDepthStencilState(listType, sortTriangles, shadowed, pp);

        return depthStencilStates[pipehash];
    }

private:
    void CreateBlitPassPipeline();
    void CreateModVolPipeline(ModVolMode mode, int cullMode, bool naomi2);
    void CreateDepthPassPipeline(int cullMode, bool naomi2);
    void CreatePipeline(u32 listType, bool sortTriangles, const PolyParam& pp, int gpuPalette, bool dithering);

    void CreateModVolDepthStencilState(ModVolMode mode, int cullMode, bool naomi2);
    void CreateDepthPassDepthStencilState(int cullMode, bool naomi2);
    void CreateDepthStencilState(u32 listType, bool sortTriangles, bool shadowed, const PolyParam& pp);

    u64 hash(u32 listType, bool sortTriangles, const PolyParam *pp, int gpuPalette, bool dithering) const
    {
        u64 hash = pp->pcw.Gouraud | (pp->pcw.Offset << 1) | (pp->pcw.Texture << 2) | (pp->pcw.Shadow << 3)
            | (((pp->tileclip >> 28) == 3) << 4);
        hash |= ((listType >> 1) << 5);
        bool ignoreTexAlpha = pp->tsp.IgnoreTexA || pp->tcw.PixelFmt == Pixel565;
        hash |= (pp->tsp.ShadInstr << 7) | (ignoreTexAlpha << 9) | (pp->tsp.UseAlpha << 10)
            | (pp->tsp.ColorClamp << 11) | ((config::Fog ? pp->tsp.FogCtrl : 2) << 12) | (pp->tsp.SrcInstr << 14)
            | (pp->tsp.DstInstr << 17);
        hash |= (pp->isp.ZWriteDis << 20) | (pp->isp.CullMode << 21) | (pp->isp.DepthMode << 23);
        hash |= ((u64)sortTriangles << 26) | ((u64)gpuPalette << 27) | ((u64)pp->isNaomi2() << 29);
        hash |= (u64)(!settings.platform.isNaomi2() && config::NativeDepthInterpolation) << 30;
        hash |= (u64)(pp->tcw.PixelFmt == PixelBumpMap) << 31;
        hash |= (u64)dithering << 32;

        return hash;
    }
    u64 hash(u32 listType, bool sortTriangles, bool shadowed, const PolyParam *pp) const
    {
        u64 hash = pp->pcw.Gouraud | (pp->pcw.Offset << 1) | (pp->pcw.Texture << 2) | (pp->pcw.Shadow << 3)
            | (((pp->tileclip >> 28) == 3) << 4);
        hash |= ((listType >> 1) << 5);
        bool ignoreTexAlpha = pp->tsp.IgnoreTexA || pp->tcw.PixelFmt == Pixel565;
        hash |= (pp->tsp.ShadInstr << 7) | (ignoreTexAlpha << 9) | (pp->tsp.UseAlpha << 10)
            | (pp->tsp.ColorClamp << 11) | ((config::Fog ? pp->tsp.FogCtrl : 2) << 12) | (pp->tsp.SrcInstr << 14)
            | (pp->tsp.DstInstr << 17);
        hash |= (pp->isp.ZWriteDis << 20) | (pp->isp.CullMode << 21) | (pp->isp.DepthMode << 23);
        hash |= ((u64)sortTriangles << 26) | ((u64)pp->isNaomi2() << 29);
        hash |= (u64)(!settings.platform.isNaomi2() && config::NativeDepthInterpolation) << 30;
        hash |= (u64)(pp->tcw.PixelFmt == PixelBumpMap) << 31;

        return hash;
    }
    u32 hash(ModVolMode mode, int cullMode, bool naomi2) const
    {
        return ((int)mode << 2) | cullMode | ((int)naomi2 << 5) | ((int)(!settings.platform.isNaomi2() && config::NativeDepthInterpolation) << 6);
    }
    u32 hash(int cullMode, bool naomi2) const
    {
        return cullMode | ((int)naomi2 << 2) | ((int)(!settings.platform.isNaomi2() && config::NativeDepthInterpolation) << 3);
    }

    MTL::VertexDescriptor* GetMainVertexInputDescriptor(bool full = true, bool naomi2 = false) const
    {
        MTL::VertexDescriptor *vertexDesc = MTL::VertexDescriptor::alloc()->init();

        auto pos = vertexDesc->attributes()->object(0); // pos
        pos->setFormat(MTL::VertexFormatFloat3);
        pos->setOffset(offsetof(Vertex, x));
        pos->setBufferIndex(30);

        if (full) {
            auto col = vertexDesc->attributes()->object(1); // base color
            col->setFormat(MTL::VertexFormatUChar4Normalized);
            col->setOffset(offsetof(Vertex, col));
            col->setBufferIndex(30);

            auto spc = vertexDesc->attributes()->object(2); // offset color
            spc->setFormat(MTL::VertexFormatUChar4Normalized);
            spc->setOffset(offsetof(Vertex, spc));
            spc->setBufferIndex(30);

            auto u = vertexDesc->attributes()->object(3); // tex coord
            u->setFormat(MTL::VertexFormatFloat2);
            u->setOffset(offsetof(Vertex, u));
            u->setBufferIndex(30);

            auto col1 = vertexDesc->attributes()->object(4);
            col1->setFormat(MTL::VertexFormatUChar4Normalized);
            col1->setOffset(offsetof(Vertex, col1));
            col1->setBufferIndex(30);

            auto spc1 = vertexDesc->attributes()->object(5);
            spc1->setFormat(MTL::VertexFormatUChar4Normalized);
            spc1->setOffset(offsetof(Vertex, spc1));
            spc1->setBufferIndex(30);

            auto u1 = vertexDesc->attributes()->object(6); // tex coord
            u1->setFormat(MTL::VertexFormatFloat2);
            u1->setOffset(offsetof(Vertex, u1));
            u1->setBufferIndex(30);

            if (naomi2) {
                auto nx = vertexDesc->attributes()->object(7); // naomi2 normal
                nx->setFormat(MTL::VertexFormatFloat3);
                nx->setOffset(offsetof(Vertex, nx));
                nx->setBufferIndex(30);
            }
        }

        auto layout = vertexDesc->layouts()->object(30);
        layout->setStride(sizeof(Vertex));
        layout->setStepRate(1);
        layout->setStepFunction(MTL::VertexStepFunctionPerVertex);

        return vertexDesc;
    }

    static inline MTL::BlendFactor GetBlendFactor(u32 instr, bool src) {
        switch (instr) {
        case 0:	// zero
			return MTL::BlendFactorZero;
        case 1: // one
			return MTL::BlendFactorOne;
        case 2: // other color
			return src ? MTL::BlendFactorDestinationColor : MTL::BlendFactorSourceColor;
		case 3: // inverse other color
			return src ? MTL::BlendFactorOneMinusDestinationColor : MTL::BlendFactorOneMinusSourceColor;
        case 4: // src alpha
			return MTL::BlendFactorSourceAlpha;
		case 5: // inverse src alpha
			return MTL::BlendFactorOneMinusSourceAlpha;
		case 6: // dst alpha
		    return MTL::BlendFactorDestinationAlpha;
		case 7: // inverse dst alpha
		    return MTL::BlendFactorOneMinusDestinationAlpha;
		default:
			die("Unsupported blend instruction");
			return MTL::BlendFactorZero;
		}
    }

    MetalRenderer *renderer;
    MTL::RenderPipelineState* blitPassPipeline = nullptr;
    std::map<u64, MTL::RenderPipelineState*> pipelines;
    std::map<u32, MTL::RenderPipelineState*> modVolPipelines;
    std::map<u32, MTL::RenderPipelineState*> depthPassPipelines;

    std::map<u32, MTL::DepthStencilState*> modVolStencilStates;
    std::map<u64, MTL::DepthStencilState*> depthStencilStates;
    std::map<u32, MTL::DepthStencilState*> depthPassDepthStencilStates;
};

static const MTL::CompareFunction depthOps[] =
{
    MTL::CompareFunctionNever,        // 0 Never
    MTL::CompareFunctionLess,         // 1 Less
    MTL::CompareFunctionEqual,        // 2 Equal
    MTL::CompareFunctionLessEqual,    // 3 Less Or Equal
    MTL::CompareFunctionGreater,      // 4 Greater
    MTL::CompareFunctionNotEqual,     // 5 Not Equal
    MTL::CompareFunctionGreaterEqual, // 6 Greater Or Equal
    MTL::CompareFunctionAlways,       // 7 Always
};