// macOS document-open support.
//
// Files opened from Finder ("Open With", double-click on an associated type, or
// a drop on the Dock icon) are delivered as a kAEOpenDocuments Apple Event, not
// as command line arguments. GLFW creates the NSApplication but installs no
// handler for it, so we register our own and queue the paths for the main loop.

#include "app/PlatformIntegration.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <deque>
#include <mutex>

namespace tessera::app::platform::detail {

std::mutex g_mutex;
std::deque<std::string> g_pending;

void enqueue(std::string path) {
    std::scoped_lock lock(g_mutex);
    g_pending.push_back(std::move(path));
}

}  // namespace tessera::app::platform::detail

/// The Apple Event target has to be an Objective-C object exposing a selector
/// of a fixed shape, so it cannot just be a C++ callable.
@interface TesseraOpenDocumentHandler : NSObject
- (void)handleOpenDocuments:(NSAppleEventDescriptor*)event
             withReplyEvent:(NSAppleEventDescriptor*)reply;
@end

@implementation TesseraOpenDocumentHandler

- (void)handleOpenDocuments:(NSAppleEventDescriptor*)event
             withReplyEvent:(NSAppleEventDescriptor*)reply {
    (void)reply;
    NSAppleEventDescriptor* list = [event paramDescriptorForKeyword:keyDirectObject];
    if (list == nil) return;

    // Apple Event descriptor lists are 1-based.
    for (NSInteger i = 1; i <= [list numberOfItems]; ++i) {
        NSAppleEventDescriptor* item = [list descriptorAtIndex:i];
        NSString* text = [item stringValue];
        if (text == nil) continue;

        // The descriptor carries a file URL rather than a POSIX path.
        NSURL* url = [NSURL URLWithString:text];
        NSString* path = [url isFileURL] ? [url path] : text;
        if (path != nil) {
            tessera::app::platform::detail::enqueue(std::string([path UTF8String]));
        }
    }
}

@end

namespace tessera::app::platform {

void installOpenFileHandler() {
    static TesseraOpenDocumentHandler* handler = nil;
    if (handler != nil) return;

    handler = [[TesseraOpenDocumentHandler alloc] init];
    [[NSAppleEventManager sharedAppleEventManager]
        setEventHandler:handler
            andSelector:@selector(handleOpenDocuments:withReplyEvent:)
          forEventClass:kCoreEventClass
             andEventID:kAEOpenDocuments];
}

bool takePendingOpenFile(std::string& path) {
    std::scoped_lock lock(detail::g_mutex);
    if (detail::g_pending.empty()) return false;
    path = std::move(detail::g_pending.front());
    detail::g_pending.pop_front();
    return true;
}

}  // namespace tessera::app::platform
