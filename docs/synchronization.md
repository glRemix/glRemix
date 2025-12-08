# Synchronization Dev Notes

IPC Milestone 2 Deliverables:

1. Event-based IPC synchronization
2. Double-buffered IPC interchange
    1. IMPORTANT: Never skip host app frame
3. Refactor to remove extra buffer used in shim via FrameRecorder

## Event Objects in Windows

`CreateEventW`: creates the initial event object. Initializes in a non-signaled state.
`WaitForSingleObject` & `WaitForMultipleObjects`: Blocks the calling thread until one or multiple events is signaled.

## Performance Analysis

### GLXGears

|         | Original App | Framerate Synchronization | Double-buffered IPC | O(n) CPU data transfer | TLAS Optimization |
| ------- | ------------ | ----------- | ----------- | ----------- | ------- |
| Debug   | 1926.2       | 486.5       | 502.7       | 739.6       | 2101.9 |
| Release | 1980.5       | 774.4       | 855.5       | 1390.8      | 2409.7 |
 
## Refactor Milestones

### Milestone 1 - Host-to-glRemixRenderer Framerate Synchronization
- Commit Hash: `b98f4ad9e0a61e7b4c88534022232a694e2d3fc5`
### Milestone 2 - Double-buffered IPC
- Commit Hash: `cba21d9bd1efaf8763b9a1a52323b5993a5110b3`
### Milestone 3 - O(n) read and write data transfer on CPU
- Commit Hash: `199a198f822e66e848457b6688d961b93c0d477e`

This milestone involves forcing synchronization of the host app and glRemix's DX12 renderer.
This connotes that the host app is not allowed to move to a new frame until the renderer has read from shared memory.

Refactor `IPCProtocol::send_frame` so that it does not allow progression of the thread until it has successfully written to shared memory.

This is done using `WaitForSingleObject` with the shared memory's read event member variable as the handle.

```cpp
DWORD dw_wait_result = WaitForSingleObject(m_smem.get_read_event(), INFINITE);
```

Shown below is the (rudimentary) debug log from the renderer with incrementing, non-skipping frames.

![debug log - incrementing frames](assets/debugLog_incrementingFrames.png)
