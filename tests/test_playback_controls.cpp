#include <initializer_list>
#include "playback_controls.h"
#include <cassert>
#include <cstring>
int main() {
  for (char k : {'[','{'}) assert(!strcmp(playbackCommand(k), "previous"));
  for (char k : {']','}'}) assert(!strcmp(playbackCommand(k), "next"));
  for (char k : {',','<'}) assert(!strcmp(playbackCommand(k), "backward"));
  for (char k : {'.','>'}) assert(!strcmp(playbackCommand(k), "forward"));
  assert(!strcmp(playbackCommand(' '), "toggle"));
  for (char k : {'p','P','n','N','8','f','F','s','S',';','/',char(0)}) assert(!playbackCommand(k));
}
