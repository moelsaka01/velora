import React from 'react';
import {Composition} from 'remotion';
import {Ad} from './Ad';

export const FPS = 30;
export const DURATION = 30 * FPS; // 900 frames

export const Root: React.FC = () => (
  <>
    <Composition id="VeloraAd" component={Ad} durationInFrames={DURATION} fps={FPS} width={1920} height={1080} defaultProps={{square: false}} />
    <Composition id="VeloraAdSquare" component={Ad} durationInFrames={DURATION} fps={FPS} width={1080} height={1080} defaultProps={{square: true}} />
  </>
);
