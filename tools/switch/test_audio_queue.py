from pathlib import Path
import subprocess,tempfile
import argparse
parser = argparse.ArgumentParser(description="Synthetic checks of the patched AUDOUT queue")
parser.add_argument('driver', type=Path, help='patched SDL_switchaudio.c')
s=parser.parse_args().driver.read_text()
start=s.index('static bool SWITCHAUDIO_PlayDevice(')
end=s.index('static void SWITCHAUDIO_CloseDevice(',start)
c='''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define NUM_AUDIO_BUFFERS 2
#define R_FAILED(r) ((r)!=0)
typedef uint8_t Uint8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef struct { void *buffer; u64 data_size; } AudioOutBuffer;
struct SDL_PrivateAudioData { Uint8 *mixbuf; AudioOutBuffer buffers[2]; bool queued[2]; int next; };
typedef struct { struct SDL_PrivateAudioData *hidden; } SDL_AudioDevice;
#define SDL_memcpy memcpy
static int waits, submits, pending, max_pending, release_slot, wait_error, append_error, empty_release;
static struct SDL_PrivateAudioData *state;
static bool SDL_SetError(const char *x) { return false; }
static int audoutAppendAudioOutBuffer(AudioOutBuffer *b) {
 if (append_error) return 1;
 assert(!state->queued[b-state->buffers]);
 ++submits; if (++pending>max_pending) max_pending=pending; return 0;
}
static int audoutWaitPlayFinish(AudioOutBuffer **b, u32 *count, u64 timeout) {
 ++waits;
 if(wait_error) return 1;
 if(empty_release) { empty_release=0; *count=0; return 0; }
 assert(pending==2);
 *b=&state->buffers[release_slot]; *count=1; --pending;
 release_slot=1-release_slot; return 0;
}
'''+s[start:end]+'''
int main(void) {
 struct SDL_PrivateAudioData h={0}; SDL_AudioDevice d={&h}; state=&h;
 unsigned char a[16],b[16],pcm[16]={1,2,3}; h.buffers[0].buffer=a; h.buffers[1].buffer=b;
 assert(SWITCHAUDIO_PlayDevice(&d,pcm,16));
 assert(SWITCHAUDIO_WaitDevice(&d)); assert(waits==0 && pending==1 && h.next==1);
 assert(SWITCHAUDIO_PlayDevice(&d,pcm,16));
 assert(SWITCHAUDIO_WaitDevice(&d)); assert(waits==1 && pending==1 && h.next==0);
 for(int i=0;i<100;i++) {
  assert(SWITCHAUDIO_PlayDevice(&d,pcm,16));
  assert(SWITCHAUDIO_WaitDevice(&d)); assert(pending==1);
 }
 assert(max_pending==2); assert(memcmp(a,pcm,16)==0 && memcmp(b,pcm,16)==0);
 // Reject overwriting a buffer still owned by AUDOUT.
 h.next=1-h.next; assert(!SWITCHAUDIO_PlayDevice(&d,pcm,16)); h.next=1-h.next;
 // A failed submission must leave its slot free.
 append_error=1; assert(!SWITCHAUDIO_PlayDevice(&d,pcm,16)); assert(!h.queued[h.next]); append_error=0;
 assert(SWITCHAUDIO_PlayDevice(&d,pcm,16));
 wait_error=1; assert(!SWITCHAUDIO_WaitDevice(&d)); wait_error=0;
 // A wakeup with no released buffer cannot authorize reuse.
 empty_release=1; assert(SWITCHAUDIO_WaitDevice(&d)); assert(pending==1);
 // Reclaim the newer slot first: choose the returned tag, not a ring assumption.
 assert(SWITCHAUDIO_PlayDevice(&d,pcm,16)); release_slot=h.next;
 int expected=release_slot; assert(SWITCHAUDIO_WaitDevice(&d)); assert(h.next==expected);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp); (p/'check.c').write_text(c)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Wno-unused-parameter',str(p/'check.c'),'-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],check=True)
print('AUDOUT queue checks passed: two buffers, sustained playback, returned tags, spurious wakeup, errors, no overwrite')
