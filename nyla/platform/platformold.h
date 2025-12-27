#pragma once
//
// #include <cstdint>
// #include <span>
// #include <string>
//
// #include "nyla/platform/key_physical.h"
//
// namespace nyla
// {
//
// struct PlatformWindowSize
// {
//     uint32_t width;
//     uint32_t height;
// };
//
// struct PlatformWindow
// {
//     uint32_t handle;
// };
//
// struct PlatformInitDesc
// {
//     bool keyboardInput;
//     bool mouseInput;
// };
//
// class Platform
// {
//   public:
//     void Init(const PlatformInitDesc &);
//
//     auto CreateWindow() -> PlatformWindow;
//
//   private:
// };
//
// void PlatformInit(PlatformInitDesc);
//
// void PlatformInputResolverBegin();
// auto PlatformInputResolve(KeyPhysical key) -> uint32_t;
// void PlatformInputResolverEnd();
//
// auto PlatformCreateWindow() -> PlatformWindow;
//
// auto PlatformGetWindowSize(PlatformWindow window) -> PlatformWindowSize;
//
// struct PlatformFileChanged
// {
//     bool seen;
//     std::string path;
// };
// void PlatformFsWatchFile(const std::string &path);
// auto PlatformFsGetFileChanges() -> std::span<PlatformFileChanged>;
//
// struct PlatformProcessEventsCallbacks
// {
//     void (*handleKeyPress)(void *user, uint32_t code);
//     void (*handleKeyRelease)(void *user, uint32_t code);
//     void (*handleMousePress)(void *user, uint32_t code);
//     void (*handleMouseRelease)(void *user, uint32_t code);
// };
//
// struct PlatformProcessEventsResult
// {
//     bool shouldRedraw;
// };
// auto PlatformProcessEvents(const PlatformProcessEventsCallbacks &callbacks, void *user) ->
// PlatformProcessEventsResult;
//
// auto PlatformShouldExit() -> bool;

// } // namespace nyla