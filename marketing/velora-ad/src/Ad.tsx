import React from 'react';
import {AbsoluteFill, Audio, Sequence, staticFile, useCurrentFrame} from 'remotion';
import {Part1} from './Part1';
import {Part2, Part2Scene3, PART2_SCENE} from './Part2';
import {Finale} from './Finale';
import {iv} from './ui';

// Swap these for licensed audio dropped into public/ (Part 1 plays from 0 s, Part 2 from 12 s).
const MUSIC_PART1 = 'music_part1.wav';
const MUSIC_PART2 = 'music_part2.wav';

// "The reverse": white glass shatters outward over the cut from Part 1 to the dark half (frames 346-400).
const SH = (() => {
  const out: {pts: string; cx: number; cy: number; rot: number; vz: number; d: number}[] = [];
  let seed = 7; const r = () => { seed = (seed * 16807) % 2147483647; return seed / 2147483647; };
  const C = 10, R = 6;
  for (let j = 0; j < R; j++) for (let i = 0; i < C; i++) {
    const x0 = (i / C) * 100, x1 = ((i + 1) / C) * 100, y0 = (j / R) * 100, y1 = ((j + 1) / R) * 100;
    for (const t of [[[x0, y0], [x1, y0], [x1, y1]], [[x0, y0], [x1, y1], [x0, y1]]]) {
      const cx = (t[0][0] + t[1][0] + t[2][0]) / 3, cy = (t[0][1] + t[1][1] + t[2][1]) / 3;
      out.push({pts: t.map(p => `${p[0]}% ${p[1]}%`).join(','), cx: cx - 50, cy: cy - 50, rot: (r() - .5) * 220, vz: .6 + r() * 1.4, d: Math.hypot(cx - 50, cy - 50) / 60 * 6 + r() * 3});
    }
  }
  return out;
})();
const Shatter: React.FC = () => {
  const f = useCurrentFrame();
  if (f < 346 || f > 400) return null;
  const g = f - 358;
  return (
    <AbsoluteFill style={{pointerEvents: 'none', perspective: 1400}}>
      {SH.map((s, i) => {
        const t = Math.max(0, g - s.d);
        const k = t * t * 0.09;
        return <div key={i} style={{position: 'absolute', inset: 0, background: 'linear-gradient(135deg,#fff,#dfeaff)', clipPath: `polygon(${s.pts})`, WebkitClipPath: `polygon(${s.pts})`,
          opacity: g < 0 ? iv(f, [346, 358], [0, 1]) : iv(t, [0, 16], [1, 0]),
          transform: `translate(${s.cx * k * 3}px, ${s.cy * k * 3 + t * t * 0.4}px) translateZ(${s.vz * t * 18}px) rotate(${s.rot * Math.min(1, t / 14)}deg)`}} />;
      })}
    </AbsoluteFill>
  );
};


// Timeline @30 fps: Part 1 0-360, Part 2 360-720 (3 x 120), Finale 720-900.
const Scene: React.FC = () => {
  const f = useCurrentFrame();
  return (
    <AbsoluteFill style={{background: '#000'}}>
      <Sequence from={0} durationInFrames={360} layout="none"><Part1 /></Sequence>
      <Sequence from={360} durationInFrames={285} layout="none"><Part2 /></Sequence>
      <Sequence from={645} durationInFrames={PART2_SCENE + 12} layout="none"><Part2Scene3 /></Sequence>
      <Shatter />
      <Sequence from={720} durationInFrames={180} layout="none">
        <AbsoluteFill style={{opacity: iv(f - 720, [0, 12], [0, 1])}}><Finale /></AbsoluteFill>
      </Sequence>
    </AbsoluteFill>
  );
};

export const Ad: React.FC<{square?: boolean}> = ({square}) => {
  const f = useCurrentFrame();
  const body = square ? (
    <AbsoluteFill style={{background: '#000'}}>
      {/* blurred, enlarged copy fills the top and bottom bands; the sharp copy is centre-cropped */}
      <div style={{position: 'absolute', left: (1080 - 1920) / 2, top: (1080 - 1080) / 2, width: 1920, height: 1080, transform: 'scale(1.7)', filter: 'blur(40px) brightness(.8)'}}><Scene /></div>
      <div style={{position: 'absolute', left: (1080 - 1920) / 2, top: (1080 - 1080) / 2, width: 1920, height: 1080, transform: 'scale(0.8)', transformOrigin: '50% 50%'}}><Scene /></div>
    </AbsoluteFill>
  ) : <Scene />;
  return (
    <AbsoluteFill style={{background: '#000'}}>
      {body}
      {/* music: part 1 loop, 1 s crossfade into the ambient part, finale resolution */}
      <Sequence from={0} durationInFrames={390}>
        <Audio src={staticFile(MUSIC_PART1)} volume={(i) => iv(i, [0, 6, 375, 390], [0, 1, 1, 0])} />
      </Sequence>
      <Sequence from={360} durationInFrames={540}>
        <Audio src={staticFile(MUSIC_PART2)} volume={(i) => iv(i, [0, 2, 480, 540], [0, 1, 1, 0])} />
      </Sequence>
    </AbsoluteFill>
  );
};
