#ifndef SPELLS_SIMPLIFY_H_INCLUDED
#define SPELLS_SIMPLIFY_H_INCLUDED

#include <QCheckBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLayout>
#include <QModelIndex>
#include <QPersistentModelIndex>
#include <QPushButton>
#include <QSettings>

class NifModel;

class SimplifyMeshDialog : public QDialog
{
	Q_OBJECT

public:
	NifModel *	nif;
	size_t	numVerts, numTriangles;
	QPersistentModelIndex	iBlock, iVertexData, iTriangles;
protected:
	// 12 floats per vertex: position (3), normal (3), UV (2), color (4)
	std::vector< float >	vertexAttrData;
	std::vector< unsigned int >	indicesData;
	std::vector< unsigned int >	newIndicesData;
	QDoubleSpinBox *	targetCount;
	QDoubleSpinBox *	maxError;
	QDoubleSpinBox *	minTriangles;
	QCheckBox *	lockBorders;
	QCheckBox *	enablePrune;
	QDoubleSpinBox *	normalWeight;
	QDoubleSpinBox *	uvWeight;
	QDoubleSpinBox *	colorWeight;
	QLabel *	resultTriangles;
	QLabel *	resultError;
	bool loadGeometryData();
public:
	void storeGeometryData( size_t newTriangleCnt );
	SimplifyMeshDialog( NifModel * nifModel, const QModelIndex & index );
	virtual ~SimplifyMeshDialog();
	inline bool isValid() const
	{
		return ( numVerts > 0 && numTriangles > 0 );
	}
public slots:
	virtual void updateIndices();
	int exec() override;
};

#endif
