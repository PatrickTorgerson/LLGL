/*
 * GLGraphicsPipeline.cpp
 * 
 * This file is part of the "LLGL" project (Copyright (c) 2015-2018 by Lukas Hermanns)
 * See "LICENSE.txt" for license information.
 */

#include "GLGraphicsPipeline.h"
#include "GLRenderPass.h"
#include "../Ext/GLExtensions.h"
#include "../../GLCommon/GLTypes.h"
#include "../../GLCommon/GLCore.h"
#include "../../CheckedCast.h"
#include "../../StaticLimits.h"
#include "../../../Core/Helper.h"
#include "../../../Core/RawBufferIterator.h"
#include <LLGL/GraphicsPipelineFlags.h>


namespace LLGL
{


/* ----- Internal functions ----- */

#if 0
static void Convert(GLStencil& dst, const StencilFaceDescriptor& src)
{
    dst.sfail        = GLTypes::Map(src.stencilFailOp);
    dst.dpfail       = GLTypes::Map(src.depthFailOp);
    dst.dppass       = GLTypes::Map(src.depthPassOp);
    dst.func         = GLTypes::Map(src.compareOp);
    dst.ref          = static_cast<GLint>(src.reference);
    dst.mask         = src.readMask;
    dst.writeMask    = src.writeMask;
}
#endif

static GLState PolygonModeToPolygonOffset(const PolygonMode mode)
{
    switch (mode)
    {
        case PolygonMode::Fill:         return GLState::POLYGON_OFFSET_FILL;
        case PolygonMode::Wireframe:    return GLState::POLYGON_OFFSET_LINE;
        case PolygonMode::Points:       return GLState::POLYGON_OFFSET_POINT;
    }
    throw std::invalid_argument("failed to map 'PolygonMode' to polygon offset mode (GL_POLYGON_OFFSET_FILL, GL_POLYGON_OFFSET_LINE, or GL_POLYGON_OFFSET_POINT)");
}

static bool IsPolygonOffsetEnabled(const DepthBiasDescriptor& desc)
{
    /* Ignore clamp factor for this check, since it's useless without the other two parameters */
    return (desc.slopeFactor != 0.0f || desc.constantFactor != 0.0f);
}


/* ----- GLGraphicsPipeline class ----- */

GLGraphicsPipeline::GLGraphicsPipeline(const GraphicsPipelineDescriptor& desc, const RenderingLimits& limits)
{
    /* Convert shader state */
    shaderProgram_ = LLGL_CAST(const GLShaderProgram*, desc.shaderProgram);
    if (!shaderProgram_)
        throw std::invalid_argument("failed to create graphics pipeline due to missing shader program");

    /* Convert input-assembler state */
    drawMode_ = GLTypes::Map(desc.primitiveTopology);

    if (IsPrimitiveTopologyPatches(desc.primitiveTopology))
    {
        /* Store patch vertices and check limit */
        const auto patchSize = GetPrimitiveTopologyPatchSize(desc.primitiveTopology);
        if (patchSize > limits.maxPatchVertices)
        {
            throw std::runtime_error(
                "renderer does not support " + std::to_string(patchVertices_) +
                " control points for patches (limit is " + std::to_string(limits.maxPatchVertices) + ")"
            );
        }
        else
            patchVertices_ = static_cast<GLint>(patchSize);
    }
    else
        patchVertices_ = 0;

    /* Create depth-stencil state */
    depthStencilState_ = GLStateManager::active->CreateDepthStencilState(desc.depth, desc.stencil);

    /* Convert rasterizer state */
    polygonMode_            = GLTypes::Map(desc.rasterizer.polygonMode);
    cullFace_               = GLTypes::Map(desc.rasterizer.cullMode);
    frontFace_              = (desc.rasterizer.frontCCW ? GL_CCW : GL_CW);
    scissorTestEnabled_     = desc.rasterizer.scissorTestEnabled;
    depthClampEnabled_      = desc.rasterizer.depthClampEnabled;
    multiSampleEnabled_     = desc.rasterizer.multiSampling.enabled;
    sampleMask_             = desc.rasterizer.multiSampling.sampleMask;
    lineSmoothEnabled_      = desc.rasterizer.antiAliasedLineEnabled;
    lineWidth_              = desc.rasterizer.lineWidth;
    polygonOffsetEnabled_   = IsPolygonOffsetEnabled(desc.rasterizer.depthBias);
    polygonOffsetMode_      = PolygonModeToPolygonOffset(desc.rasterizer.polygonMode);
    polygonOffsetFactor_    = desc.rasterizer.depthBias.slopeFactor;
    polygonOffsetUnits_     = desc.rasterizer.depthBias.constantFactor;
    polygonOffsetClamp_     = desc.rasterizer.depthBias.clamp;

    #ifdef LLGL_GL_ENABLE_VENDOR_EXT
    conservativeRaster_ = desc.rasterizer.conservativeRasterization;
    #endif

    /* Create blend state from context state */
    if (auto renderPass = desc.renderPass)
    {
        auto renderPassGL = LLGL_CAST(const GLRenderPass*, renderPass);
        blendState_ = GLStateManager::active->CreateBlendState(desc.blend, renderPassGL->GetNumColorAttachments());
    }
    else
        blendState_ = GLStateManager::active->CreateBlendState(desc.blend, 1);

    /* Build static state buffer for viewports and scissors */
    if (!desc.viewports.empty() || !desc.scissors.empty())
        BuildStaticStateBuffer(desc);
}

GLGraphicsPipeline::~GLGraphicsPipeline()
{
    if (blendState_.use_count() == 2)
    {
        blendState_.reset();
        GLStateManager::active->ReleaseUnusedBlendStates(true);
    }
}

void GLGraphicsPipeline::Bind(GLStateManager& stateMngr)
{
    /* Bind shader program and discard rasterizer if there is no fragment shader */
    stateMngr.BindShaderProgram(shaderProgram_->GetID());
    stateMngr.Set(GLState::RASTERIZER_DISCARD, !shaderProgram_->HasFragmentShader());

    /* Set input-assembler state */
    if (patchVertices_ > 0)
        stateMngr.SetPatchVertices(patchVertices_);

    /* Set depth states */
    stateMngr.SetDepthStencilState(depthStencilState_.get());

    /* Set rasterizer state */
    stateMngr.SetPolygonMode(polygonMode_);
    stateMngr.SetFrontFace(frontFace_);

    if (cullFace_ != 0)
    {
        stateMngr.Enable(GLState::CULL_FACE);
        stateMngr.SetCullFace(cullFace_);
    }
    else
        stateMngr.Disable(GLState::CULL_FACE);

    if (polygonOffsetEnabled_)
    {
        stateMngr.Enable(polygonOffsetMode_);
        stateMngr.SetPolygonOffset(polygonOffsetFactor_, polygonOffsetUnits_, polygonOffsetClamp_);
    }
    else
        stateMngr.Disable(polygonOffsetMode_);

    stateMngr.Set(GLState::SCISSOR_TEST, scissorTestEnabled_);
    stateMngr.Set(GLState::DEPTH_CLAMP, depthClampEnabled_);
    stateMngr.Set(GLState::MULTISAMPLE, multiSampleEnabled_);
    stateMngr.Set(GLState::LINE_SMOOTH, lineSmoothEnabled_);
    stateMngr.SetLineWidth(lineWidth_);

    #ifdef LLGL_GL_ENABLE_VENDOR_EXT
    stateMngr.Set(GLStateExt::CONSERVATIVE_RASTERIZATION, conservativeRaster_);
    #endif

    #if 0//TODO
    if (multiSampleEnabled_)
        stateMngr.SetSampleMask(sampleMask_);
    #endif

    /* Set blend state */
    stateMngr.BindBlendState(blendState_.get());

    /* Set static viewports and scissors */
    if (staticStateBuffer_)
    {
        RawBufferIterator rawBufferIter { staticStateBuffer_.get() };
        if (numStaticViewports_ > 0)
            SetStaticViewports(stateMngr, rawBufferIter);
        if (numStaticScissors_ > 0)
            SetStaticScissors(stateMngr, rawBufferIter);
    }
}


/*
 * ======= Private: =======
 */

void GLGraphicsPipeline::BuildStaticStateBuffer(const GraphicsPipelineDescriptor& desc)
{
    /* Allocate packed raw buffer */
    const std::size_t bufferSize =
    (
        desc.viewports.size() * (sizeof(GLViewport) + sizeof(GLDepthRange)) +
        desc.scissors.size()  * (sizeof(GLScissor))
    );
    staticStateBuffer_ = MakeUniqueArray<char>(bufferSize);

    RawBufferIterator rawBufferIter { staticStateBuffer_.get() };

    /* Build static viewports in raw buffer */
    if (!desc.viewports.empty())
        BuildStaticViewports(desc.viewports.size(), desc.viewports.data(), rawBufferIter);

    /* Build static scissors in raw buffer */
    if (!desc.scissors.empty())
        BuildStaticScissors(desc.scissors.size(), desc.scissors.data(), rawBufferIter);
}

void GLGraphicsPipeline::BuildStaticViewports(std::size_t numViewports, const Viewport* viewports, RawBufferIterator& rawBufferIter)
{
    /* Store number of viewports and validate limit */
    numStaticViewports_ = static_cast<GLsizei>(numViewports);

    if (numStaticViewports_ > LLGL_MAX_NUM_VIEWPORTS_AND_SCISSORS)
    {
        throw std::invalid_argument(
            "too many viewports in graphics pipeline state (" + std::to_string(numStaticViewports_) +
            " specified, but limit is " + std::to_string(LLGL_MAX_NUM_VIEWPORTS_AND_SCISSORS) + ")"
        );
    }

    /* Build <GLViewport> entries */
    for (std::size_t i = 0; i < numViewports; ++i)
    {
        auto dst = rawBufferIter.Next<GLViewport>();
        {
            dst->x      = static_cast<GLfloat>(viewports[i].x);
            dst->y      = static_cast<GLfloat>(viewports[i].y);
            dst->width  = static_cast<GLfloat>(viewports[i].width);
            dst->height = static_cast<GLfloat>(viewports[i].height);
        }
    }

    /* Build <GLDepthRange> entries */
    for (std::size_t i = 0; i < numViewports; ++i)
    {
        auto dst = rawBufferIter.Next<GLDepthRange>();
        {
            dst->minDepth = static_cast<GLdouble>(viewports[i].minDepth);
            dst->maxDepth = static_cast<GLdouble>(viewports[i].maxDepth);
        }
    }
}

void GLGraphicsPipeline::BuildStaticScissors(std::size_t numScissors, const Scissor* scissors, RawBufferIterator& rawBufferIter)
{
    /* Store number of scissors and validate limit */
    numStaticScissors_ = static_cast<GLsizei>(numScissors);

    if (numStaticScissors_ > LLGL_MAX_NUM_VIEWPORTS_AND_SCISSORS)
    {
        throw std::invalid_argument(
            "too many scissors in graphics pipeline state (" + std::to_string(numStaticScissors_) +
            " specified, but limit is " + std::to_string(LLGL_MAX_NUM_VIEWPORTS_AND_SCISSORS) + ")"
        );
    }

    /* Build <GLScissor> entries */
    for (std::size_t i = 0; i < numScissors; ++i)
    {
        auto dst = rawBufferIter.Next<GLScissor>();
        {
            dst->x      = static_cast<GLint>(scissors[i].x);
            dst->y      = static_cast<GLint>(scissors[i].y);
            dst->width  = static_cast<GLsizei>(scissors[i].width);
            dst->height = static_cast<GLsizei>(scissors[i].height);
        }
    }
}

void GLGraphicsPipeline::SetStaticViewports(GLStateManager& stateMngr, RawBufferIterator& rawBufferIter)
{
    /* Copy viewports to intermediate array (must be adjusted when framebuffer is bound) */
    GLViewport intermediateViewports[LLGL_MAX_NUM_VIEWPORTS_AND_SCISSORS];
    ::memcpy(
        intermediateViewports,
        rawBufferIter.Next<GLViewport>(numStaticViewports_),
        numStaticViewports_ * sizeof(GLViewport)
    );
    stateMngr.SetViewportArray(0, numStaticViewports_, intermediateViewports);

    /* Set depth ranges */
    stateMngr.SetDepthRangeArray(0, numStaticViewports_, rawBufferIter.Next<GLDepthRange>(numStaticViewports_));
}

void GLGraphicsPipeline::SetStaticScissors(GLStateManager& stateMngr, RawBufferIterator& rawBufferIter)
{
    /* Copy scissors to intermediate array (must be adjusted when framebuffer is bound) */
    GLScissor intermediateScissors[LLGL_MAX_NUM_VIEWPORTS_AND_SCISSORS];
    ::memcpy(
        intermediateScissors,
        rawBufferIter.Next<GLScissor>(numStaticScissors_),
        numStaticScissors_ * sizeof(GLScissor)
    );
    stateMngr.SetScissorArray(0, numStaticScissors_, intermediateScissors);
}


} // /namespace LLGL



// ================================================================================
