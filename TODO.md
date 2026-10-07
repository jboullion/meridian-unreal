# TODO

## Current TODOs

### UI
- The spell and skill menu should be searchable by a search bar at the top of the menu
- Right now the icon and text have separate hover regions which means hovering between them loses the dialog. Ideally they would share one wrapping hover region for the dialog
- Add some dividers between the schools
- Allow the different schools to be collapsed to save room in the menu.
- There should be a longer list of attributes available on server 104 we want to display.
  - We eventually want to support both the original, server 104, and our own Shards server as a client (that might never happen). All servers could have different lists here. Let's make sure we are listing these values from a server dependent source.
  - This includes things like unbound energy, training points, bulk carried, etc. ![face reference](ReferenceImages/issues/server-104-attributes.png)
  - The unbound energy, training points should be at the top and slighly separated as these are spendable points.
  - Bulk Carried and other properties can go below in a separated section since these are all "secondary" attributes.
  - We don't need to show the Health, Mana, and Vigor here since a user can see that in their hotbar.
- The health and stamina bars show their values, let's also show the stamina values

### Props
- Sign posts, and other props, might need a property for "rotation" in order to get them pointing the the ideal direction since they are not just sprites that always face the player now.
  - Most sprites are meant to be viewed the same from all angles so not a problem.
  - The sign needs to turn 90 degress counter clockwise by default. But some signs might need other adjustments. Would it make sense to have two properties: Direction and rotation. So we could set signs as "South" or "Down" direction by default and then just offset that rotation for specific signs? Or do we just have rotation and give signs a default of -90?
- Lamps are looking GREAT!
  - Is it possible to remove the emmisive effects from the glass and potentially just make it transparent during the day instead of a solid gray? It should look the same at night.
- Side View Props:
  - Some props don't have a side view, but could benefit from one. In these instances we would just use the same image as the front and side view
  - List:
    - Dung
    - Rocks, large and small. Otherwise they appear fairly flat
- Users should be able to walk through the wheat and other tall grasses with a movement reduction. 
  - 

- Add blockers:
 - Chests
 - Signs? (This one might need a lot of testing as some signs might block paths otherwise)
 - Trees (Ideally just the trunks)
 - 


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
