//
//  MetalRenderer.h
//  libx64
//
//  Created by Andy Qua on 21/12/2021.
//

#import <MetalKit/MetalKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface MetalRenderer : NSObject<MTKViewDelegate>
- (nonnull instancetype)initWithMetalKitView:(nonnull MTKView *)mtkView;
- (void) updateTexture:(id<MTLTexture>) t;
@end

NS_ASSUME_NONNULL_END
