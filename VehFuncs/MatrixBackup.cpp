#include "Matrixbackup.h"

void RestoreMatrixBackup(RwMatrix* dest, MatrixBackup* backup) {

	if ((int)backup > 0x01000000) 
	{
		dest->at.x = backup->at.x;
		dest->at.y = backup->at.y;
		dest->at.z = backup->at.z;

		dest->pos.x = backup->pos.x;
		dest->pos.y = backup->pos.y;
		dest->pos.z = backup->pos.z;

		dest->right.x = backup->right.x;
		dest->right.y = backup->right.y;
		dest->right.z = backup->right.z;

		dest->up.x = backup->up.x;
		dest->up.y = backup->up.y;
		dest->up.z = backup->up.z;

		RwMatrixUpdate(dest);
	}

	return;
}

MatrixBackup* CreateMatrixBackup(RwMatrix* origin) 
{
	MatrixBackup *backup = new MatrixBackup();

	backup->at.x = origin->at.x;
	backup->at.y = origin->at.y;
	backup->at.z = origin->at.z;

	backup->pos.x = origin->pos.x;
	backup->pos.y = origin->pos.y;
	backup->pos.z = origin->pos.z;

	backup->right.x = origin->right.x;
	backup->right.y = origin->right.y;
	backup->right.z = origin->right.z;

	backup->up.x = origin->up.x;
	backup->up.y = origin->up.y;
	backup->up.z = origin->up.z;

	return backup;
}

