"""Exercise the actual JXA query with simulated macOS applications using Node.js."""
import json
import subprocess
import unittest
from unittest.mock import patch
from bridge import MusicController

class PlayerSelectionTests(unittest.TestCase):
    def query(self, states, preferred='Music', controller=None):
        controller = controller or MusicController()
        controller.active_player = preferred
        controller.last_query_time = 0
        run = subprocess.run
        def osascript(args, **kwargs):
            prelude = '''
            const states = %s;
            function Application(name) {
                const state = states[name];
                return {running: () => !!state, playerState: () => state,
                    playerPosition: () => 42,
                    currentTrack: {name: () => name + " song", artist: () => "Artist",
                        album: () => "Album", duration: () => name === "Spotify" ? 180000 : 180,
                        artworks: {length: 0}, artworkUrl: () => ""}};
            }
            ''' % json.dumps(states)
            return run(['node', '-e', prelude + '\nconsole.log(' + args[-1].strip().rstrip(';') + ')'],
                       capture_output=True, text=True, check=True)
        with patch('bridge.subprocess.run', side_effect=osascript):
            return controller.query()

    def test_paused_music_with_idle_spotify(self):
        meta = self.query({'Music': 'paused', 'Spotify': 'stopped'})
        self.assertEqual((meta['player'], meta['state'], meta['title'], meta['elapsed']),
                         ('Music', 'paused', 'Music song', 42))
        self.assertTrue(meta['running'])

    def test_paused_spotify_with_idle_music(self):
        meta = self.query({'Music': 'stopped', 'Spotify': 'paused'})
        self.assertEqual((meta['player'], meta['state'], meta['duration']), ('Spotify', 'paused', 180))

    def test_playing_takes_priority(self):
        self.assertEqual(self.query({'Music': 'paused', 'Spotify': 'playing'})['player'], 'Spotify')
        self.assertEqual(self.query({'Music': 'playing', 'Spotify': 'paused'}, 'Spotify')['player'], 'Music')

    def test_preferred_paused_player_retained(self):
        self.assertEqual(self.query({'Music': 'paused', 'Spotify': 'paused'}, 'Spotify')['player'], 'Spotify')

    def test_pause_preserves_artwork(self):
        controller = MusicController()
        self.query({'Music': 'playing'}, controller=controller)
        controller.cached_art_id = 'cached-cover'
        before = controller.cached_rgb565
        meta = self.query({'Music': 'paused', 'Spotify': 'stopped'}, controller=controller)
        self.assertEqual(meta['artwork_id'], 'cached-cover')
        self.assertEqual(before, controller.cached_rgb565)

    def test_idle_is_still_stopped(self):
        for states in ({}, {'Music': 'stopped', 'Spotify': 'stopped'}):
            meta = self.query(states)
            self.assertEqual((meta['state'], meta['title']), ('stopped', ''))

if __name__ == '__main__':
    unittest.main()
