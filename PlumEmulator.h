//
//  PlumEmulator.h
//  Plum
//
//  Created by Jarrod Norwell on 8/11/2025.
//

#import <AVKit/AVKit.h>
#import <AudioUnit/AudioUnit.h>
#import <Foundation/Foundation.h>

#define SECOND_NS 1000000000

NS_ASSUME_NONNULL_BEGIN

@interface PlumEmulator : NSObject
@property (nonatomic, strong, nullable) void (^framebuffer) (uint32_t*, NSInteger, NSInteger);

+(PlumEmulator *) sharedInstance NS_SWIFT_NAME(shared());

-(NSArray<NSString *> *) insertCartridge:(NSURL *)url NS_SWIFT_NAME(insert(_:));
-(NSArray<NSString *> *) insertGenesis:(NSURL *)url;
-(NSArray<NSString *> *) insertMegaDrive:(NSURL *)url;

-(void) start;
-(void) pause:(BOOL)pause;
-(BOOL) isPaused;
-(void) stop;

-(void) updateSettings;

-(void) input:(NSInteger)slot button:(uint32_t)button pressed:(BOOL)pressed;
@end

NS_ASSUME_NONNULL_END
