#pragma once

#include "CoreMinimal.h"
#include "Engine/StaticMeshActor.h"
#include "MROfflineSprite.generated.h"

/**
 * The original sprite of an object whose prop props.json "mesh_keep" leaves out (docs/adr/0007 "Fewer props"),
 * placed by the world build (tools/ue/build_world.py, tools/environment/object_sprites.py): a quad its material
 * (M_ObjectSprite) turns to the camera. For offline play only: online the server sends the object and draws it
 * from its own bitmap (UMRNetWorldSubsystem, UMRBgfSpriteComponent), so this one hides itself.
 */
UCLASS()
class UNREALMERIDIAN_API AMROfflineSprite : public AStaticMeshActor
{
	GENERATED_BODY()

public:
	AMROfflineSprite(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void BeginPlay() override;
};
