# TODO

## Current TODOs

### UI


### Props
- Prop height ![Chandlier height](ReferenceImages/issues/chandalier-height.png)
  - I suspect when we build our unreal maps we are not taking into account the height offset (or some similar property). Causing objects like the chandelier to be on the floor instead on the cieling.


### Movement

- Remove crouching, jumping, and require a user to hit space bar to travel between levels.
- "Run" should be the default movement, and walk should happen on shift.


### Server




### Character

- The face and the right arm when viewed close up have some artifacts likely cause by edges / seams of the face parts ![face reference](ReferenceImages/issues/face-image-artifacts.png)
  - We might just want to remove the AI image generation step and perhaps just see what things like like with the upscalers or maybe even the original images.

- The mummy (the only monster I have seen) seems to bounce back and forth a bit as their walk animation plays. I think perhaps there is an offset between sheets with the sprites being different sizes making it flipbook cause this strnage jitter effect.

## Admins and Guides

- We will need to have some accounts be able to be "mods" or "guides" who have access to the various god powers and skills for admin purposes.
  - These users should also be able to teleport to players. Start events. etc.
