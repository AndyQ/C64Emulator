//
//  MetalRenderer.m
//  libx64
//
//  Created by Andy Qua on 21/12/2021.
//

#import <Foundation/Foundation.h>
#import "MetalRenderer.h"
#include <simd/simd.h>


NSString* shaderSrc = @"#include <metal_stdlib> \n"
"using namespace metal; \n"
"\n"
"struct VertexInTexture { \n"
"    float4 position [[ attribute((0)) ]]; \n"
"    float2 textureCoordinates [[ attribute(1) ]]; \n"
"}; \n"
"\n"
"struct VertexOutTexture { \n"
"    float4 position [[ position ]]; \n"
"    float2 textureCoordinates; \n"
"}; \n"
"\n"
"\n"
"vertex VertexOutTexture vertex_shader_texture(const VertexInTexture vertexIn [[ stage_in ]]) { \n"
"    VertexOutTexture vertexOut; \n"
"    vertexOut.position = vertexIn.position; \n"
"    vertexOut.textureCoordinates = vertexIn.textureCoordinates; \n"
"    return vertexOut; \n"
"} \n"
"\n"
"fragment half4 fragment_shader_texture(VertexOutTexture vertexIn [[ stage_in ]], \n"
"                                       texture2d<float> texture [[ texture((0)) ]]) { \n"
" \n"
"    constexpr sampler linearSampler (mip_filter::none, \n"
"                                     mag_filter::linear, \n"
"                                     min_filter::linear); \n"
"\n"
"    float4 shade = texture.sample(linearSampler, vertexIn.textureCoordinates); \n"
"    return half4(shade.r, shade.g, shade.b, 1); \n"
"} \n";


typedef struct  {
    vector_float4 position;
    vector_float2 texture;
} VertexTexture;


VertexTexture verticesTexture[] = {
    { {-1, 1, 0, 1}, {0, 0} },
    { {-1, -1, 0, 1}, {0, 1} },
    { {1, -1, 0, 1}, {1, 1} },
    { {1, 1, 0, 1}, {1, 0} },
};

UInt16 indicesTexture[] = { 0, 1, 2, 2, 3, 0 };

@implementation MetalRenderer
{
    id<MTLDevice>        _device;
    id<MTLCommandQueue>  _commandQueue;
    
    id<MTLRenderPipelineState>     pipelineTexture;
    
    id<MTLBuffer> verticesBufferTexture;
    id<MTLBuffer> indicesBufferTexture;
    
    id<MTLTexture> texture;
}

- (nonnull instancetype)initWithMetalKitView:(nonnull MTKView *)mtkView
{
    self = [super init];
    if(self)
    {
        _device = mtkView.device;
        _commandQueue = [_device newCommandQueue];
        
        mtkView.colorPixelFormat = MTLPixelFormatBGRA8Unorm;

        [self setupBuffers];
    }
    
    return self;
}

- (void) updateTexture:(id<MTLTexture>) t {
    texture = t;
}

- (void) mtkView:(nonnull MTKView *)view drawableSizeWillChange:(CGSize)size {
    
}

- (void) setupBuffers {
    
    NSError *error = nil;
    id<MTLLibrary> library = [_device newLibraryWithSource:shaderSrc options:nil error:&error];
    id<MTLFunction> vertexShader = [library newFunctionWithName:@"vertex_shader_texture"];
    id<MTLFunction> fragmentShader = [library newFunctionWithName:@"fragment_shader_texture"];
    
    MTLVertexDescriptor *mtlVertexDescriptor = [[MTLVertexDescriptor alloc] init];

    // place
    mtlVertexDescriptor.attributes[0].format = MTLVertexFormatFloat4;
    mtlVertexDescriptor.attributes[0].offset = 0;
    mtlVertexDescriptor.attributes[0].bufferIndex = 0;
    
    // texture
    mtlVertexDescriptor.attributes[1].format = MTLVertexFormatFloat2;
    mtlVertexDescriptor.attributes[1].offset = 16;
    mtlVertexDescriptor.attributes[1].bufferIndex = 0;
    
    mtlVertexDescriptor.layouts[0].stride = sizeof(VertexTexture);
    
    MTLRenderPipelineDescriptor *pipelineStateDescriptor = [MTLRenderPipelineDescriptor new];
    pipelineStateDescriptor.label                        = @"Temple Pipeline";

    pipelineStateDescriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;//_sRGB;
    
    pipelineStateDescriptor.vertexFunction = vertexShader;
    pipelineStateDescriptor.fragmentFunction = fragmentShader;

    pipelineStateDescriptor.vertexDescriptor = mtlVertexDescriptor;
    
    pipelineTexture = [_device newRenderPipelineStateWithDescriptor:pipelineStateDescriptor
                                                                    error:nil];
        
    verticesBufferTexture = [_device newBufferWithBytes:&verticesTexture length:sizeof(verticesTexture) options:0];
    indicesBufferTexture = [_device newBufferWithBytes:indicesTexture length:sizeof(indicesTexture) options:0];

}

- (void) drawInMTKView:(nonnull MTKView *)view {
    id<MTLCommandBuffer> commandBuffer;
    if ( texture == nil ) {
        return;
    }

    commandBuffer = [_commandQueue commandBuffer];
    commandBuffer.label = @"Drawable Command Buffer";
    
    MTLRenderPassDescriptor *drawableRenderPassDescriptor = view.currentRenderPassDescriptor;

    id<MTLRenderCommandEncoder> renderEncoder =
    [commandBuffer renderCommandEncoderWithDescriptor:drawableRenderPassDescriptor];
    renderEncoder.label = @"Drawable Render Encoder";
    
    [renderEncoder setRenderPipelineState:pipelineTexture];

    [renderEncoder setVertexBuffer:verticesBufferTexture
                            offset:0
                           atIndex:0];
    [renderEncoder setFragmentTexture:texture
                              atIndex:0];

    [renderEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                              indexCount:6
                               indexType:MTLIndexTypeUInt16
                             indexBuffer:indicesBufferTexture
                       indexBufferOffset:0];
    
    [renderEncoder endEncoding];
    [commandBuffer presentDrawable:view.currentDrawable];
    [commandBuffer commit];

}
@end
