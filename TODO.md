# TODO

## Current TODOs

### UI


### Props
- let's update the signs to use the "standard mesh" instead of the HD model
- Lamps should have their light moved down a bit to fit inside the post. Could we make the inside / glass texture glow? ![lamp reference](ReferenceImages/issues/lamp-lighting-position.png)
- Let's update the stools the same way we did the tables to they have a more similar appearance (if possible). Right now they are a little bit brighter ![stool reference](ReferenceImages/issues/table-vs-chairs-lighting.png)
- Users should be able to walk through the wheat and other tall grasses with a movement reduction. Code for that should be in the old game as well
  - This might be an issue with the surrounding wheat texture causing blocking
  - There is a small field with a lot of props on it that has a tiny grass texture around the border. That grass texture is blocking movement

- I think a lot of props should still not have blocking elements, but perhaps we can add a property to the props.json, or some other file, to choose which props do block movement. In the original game no props do. Let's start with lamps, braziers, and tables.
- Create Apple Trees, and Other types of trees in blender kit if possible.


### Movement

- Remove crouching, jumping, and require a user to hit space bar to travel between levels.
- "Run" should be the default movement, and walk should happen on shift. Also, our stamina is only used when attacking / casting spells and not when running.

### Server

- We already have a server available locally and deployed as referenced in our Meridian Shards project E:\2026_Experiments\meridian-browser\docs\roadmap.md
- Connect to our local and deployed server for testing. Our Unreal engine should basically just try to be a full client build for any Meridian Server. We might want to set up some kind of optional client / server selector so a user could use this client with the original Meridian 59 and the open source projects like 104 and our own "Shards" implementation
- We can now start implementing real server data for users, inventory, sprites sync, etc.


### Character

- The face and the right arm when viewed close up have some artifacts likely cause by edges / seams of the face parts ![face reference](ReferenceImages/issues/face-image-artifacts.png)
  - We might just want to remove the AI image generation step and perhaps just see what things like like with the upscalers or maybe even the original images.


## Admins and Guides

- We will need to have some accounts be able to be "mods" or "guides" who have access to the various god powers and skills for admin purposes.
  - These users should also be able to teleport to players. Start events. etc.
