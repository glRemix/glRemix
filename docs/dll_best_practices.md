# DLL Best Practices Dev Notes

## Fragility of `DllMain`

Best practice is to do nothing important in `DllMain`.
Especially call process-management `kernel32.dll` functions such as `CreateProcess`, `TerminateProcess`,
`CreateThread`, `ExitThread`, etc.

This is because the loader lock is enabled within DllMain. 
i.e. all other processes are in standstill or on watch while your DLL is in the spotlight. 
No other DLLs can load (run their respective `DllMain` functions), 
so `LoadLibrary` and `CreateProcess` are strictly off-limits, for obvious direct and indirect implications, respectively. 
As a side note, such indirect scenarios for the latter that are interesting
to ponder would be triggering of code integrity checks (we know the familiar pop-up), antivirus hooks, and other members of Windows innumerable collection of subsystems, 
which are / invoke DLLs themselves.

It's already seeming like a slippery slope, 
but let's think more about why everyone seems to emphasize to "DO ABSOLUTELY NOTHING" in `DllMain`.
And the question of whether we should call `gl_loader::initialize()` within `DLL_PROCESS_ATTACH`
is a perfect scenario to unpack all of the possible pathways to failure.

Most obvious is that `initialize()` tries to create the renderer process. 
Blasphemy, as we've established,
but let's ignore that instant disqualification for our current purposes.
Let's consider that it calls `IPCProtocol::init_writer()`, 
which in turn invokes a call to `kernel32.dll`'s `CreateFileMappingW` within `SharedMemory::create_for_writer()`.
Although having WinAPI identity,
`CreateFileMappingW` is low in danger level and we probably would have been alright to keep it.
But even so, think about how kernel methods are commonly hooked by other shims. 
So even here there's still no guarantee of not violating loader lock.

But next, see how we call `IPCProtocol::start_frame_or_wait()` right after, 
as we wanted to ensure all data sent to OpenGL by the host app were captured into IPC even if `wglCreateContext()` is not the first hooked command our shim intercepts. 
But the idea of waiting within DLLMain can be extremely fatal.
Yes, in our specific scenario, it worked, 
as the "can read" condition is true on initialization:

```cpp
m_read_event = CreateEventW(nullptr, false, true, read_event_name);
```

And this allowed `WaitForMultipleObjects` to return instantly. 
But hopefully you can see how fragile this whole situation was, 
relying solely on developer domain knowledge. 
Any kind of race condition, and we are officially in deadlock.

There we have it. 
Within that one initialization pipeline, we had three levels of violations:

-   Medium: Calling a `kernel32.dll` method
-   High: Waiting on a synchronization object
-   Fatal: Calling `CreateProcess`

Such a deep dive was actually quite useful, 
so going forward we can be hyperaware of this "global DLL contract" within the Windows development ecosystem.

TLDR: "With great power comes great responsibility" comes to mind...

## MS Documentation

-   [2006 - Dynamic-Link Library Best Practices](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices)

## Fun Reads

-   [2004 - _Another reason not to do anything scary in your DllMain: Inadvertent deadlock_](https://devblogs.microsoft.com/oldnewthing/20040128-00/?p=40853)
    -   TODO: Implement a lock hierarchy for our DLL!
