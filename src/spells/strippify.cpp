#include "spellbook.h"

#include "blocks.h"
#include "gl/gltools.h"

#include "lib/meshoptimizer/src/meshoptimizer.h"
#include "lib/nvtristripwrapper.h"

#include <climits>


// TODO: Move these to blocks.h / misc.h / wherever
template <typename T> void copyArray( NifModel * nif, const QModelIndex & iDst, const QModelIndex & iSrc )
{
	if ( iDst.isValid() && iSrc.isValid() ) {
		nif->updateArraySize( iDst );
		nif->setArray<T>( iDst, nif->getArray<T>( iSrc ) );
	}
}

template <typename T> void copyArray( NifModel * nif, const QModelIndex & iDst, const QModelIndex & iSrc, const QString & name )
{
	copyArray<T>( nif, nif->getIndex( iDst, name ), nif->getIndex( iSrc, name ) );
}

template <typename T> void copyValue( NifModel * nif, const QModelIndex & iDst, const QModelIndex & iSrc, const QString & name )
{
	nif->set<T>( iDst, name, nif->get<T>( iSrc, name ) );
}


class spStrippify final : public Spell
{
	QString name() const override final { return Spell::tr( "Stripify" ); }
	QString page() const override final { return Spell::tr( "Mesh" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		return nif->checkVersion( 0x0a000000, 0 ) && nif->isNiBlock( index, "NiTriShape" );
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
	{
		QPersistentModelIndex idx = index;
		QPersistentModelIndex iData = nif->getBlockIndex( nif->getLink( idx, "Data" ), "NiTriShapeData" );

		if ( !iData.isValid() )
			return idx;

		QVector<Triangle> triangles;
		QModelIndex iTriangles = nif->getIndex( iData, "Triangles" );

		if ( !iTriangles.isValid() )
			return idx;

		[[maybe_unused]] int skip = 0;

		for ( int t = 0; t < nif->rowCount( iTriangles ); t++ ) {
			Triangle tri = nif->get<Triangle>( nif->getIndex( iTriangles, t ) );

			if ( tri[0] != tri[1] && tri[1] != tri[2] && tri[2] != tri[0] )
				triangles.append( tri );
			else
				skip++;
		}

		//qDebug() << "num triangles" << triangles.count() << "skipped" << skip;


		QVector<QVector<quint16> > strips = stripify( triangles, true );

		if ( strips.count() <= 0 )
			return idx;

		uint numTriangles = 0;
		for ( const QVector<quint16>& strip : strips ) {
			numTriangles += strip.count() - 2;
		}

		if ( numTriangles > USHRT_MAX * 2 ) {
			Message::append( tr( "Strippify failed on one or more blocks." ),
				tr( "Block %1: Too many triangles (%2) to strippify this shape." )
				.arg( nif->getBlockNumber( idx ) )
				.arg( numTriangles )
			);
			return idx;
		}

		QModelIndex iStripData = nif->insertNiBlock( "NiTriStripsData", nif->getBlockNumber( idx ) + 1 );

		if ( iStripData.isValid() ) {
			copyValue<int>( nif, iStripData, iData, "Num Vertices" );

			nif->set<int>( iStripData, "Has Vertices", 1 );
			copyArray<Vector3>( nif, iStripData, iData, "Vertices" );

			copyValue<int>( nif, iStripData, iData, "Has Normals" );
			copyArray<Vector3>( nif, iStripData, iData, "Normals" );

			if ( nif->getVersionNumber() <= 0x04000002 )
				copyValue<int>( nif, iStripData, iData, "Has UV" );
			if ( !( nif->getVersionNumber() == 0x14020007 && nif->getBSVersion() > 0 ) )
				copyValue<int>( nif, iStripData, iData, "Data Flags" );
			else
				copyValue<int>( nif, iStripData, iData, "BS Data Flags" );
			copyArray<Vector3>( nif, iStripData, iData, "Bitangents" );
			copyArray<Vector3>( nif, iStripData, iData, "Tangents" );

			copyValue<int>( nif, iStripData, iData, "Has Vertex Colors" );
			copyArray<Color4>( nif, iStripData, iData, "Vertex Colors" );

			QModelIndex iDstUV = nif->getIndex( iStripData, "UV Sets" );
			QModelIndex iSrcUV = nif->getIndex( iData, "UV Sets" );

			if ( iDstUV.isValid() && iSrcUV.isValid() ) {
				nif->updateArraySize( iDstUV );

				for ( int r = 0; r < nif->rowCount( iDstUV ); r++ ) {
					copyArray<Vector2>( nif, nif->getIndex( iDstUV, r ), nif->getIndex( iSrcUV, r ) );
				}
			}

			if ( nif->getVersionNumber() >= 0x0A000100 )
				copyValue<quint16>( nif, iStripData, iData, "Consistency Flags" );

			auto bound = BoundSphere( nif, iStripData );
			bound.update( nif, iData );

			nif->set<int>( iStripData, "Num Strips", strips.count() );
			nif->set<int>( iStripData, "Has Points", 1 );

			QModelIndex iLengths = nif->getIndex( iStripData, "Strip Lengths" );
			QModelIndex iPoints  = nif->getIndex( iStripData, "Points" );

			if ( iLengths.isValid() && iPoints.isValid() ) {
				nif->updateArraySize( iLengths );
				nif->updateArraySize( iPoints );
				int x = 0;
				for ( const QVector<quint16>& strip : strips ) {
					nif->set<int>( nif->getIndex( iLengths, x ), strip.count() );
					QModelIndex iStrip = nif->getIndex( iPoints, x );
					nif->updateArraySize( iStrip );
					nif->setArray<quint16>( iStrip, strip );
					x++;
				}
				nif->set<int>( iStripData, "Num Triangles", numTriangles );

				nif->setData( idx.sibling( idx.row(), NifModel::NameCol ), "NiTriStrips" );
				int lnk = nif->getLink( idx, "Data" );
				nif->setLink( idx, "Data", nif->getBlockNumber( iStripData ) );
				nif->removeNiBlock( lnk );
			}
		}

		// Move the triangles over 65535 into their own shape by
		// splitting the two strips between two NiTriStrips
		if ( numTriangles > USHRT_MAX ) {
			spDuplicateBranch dupe;

			// Copy the entire NiTriStrips branch
			auto iStrip2 = dupe.cast( nif, idx );
			auto iStrip2Data = nif->getBlockIndex( nif->getLink( iStrip2, "Data" ), "NiTriStripsData" );
			if ( !iStrip2Data.isValid() || strips.count() != 2 )
				return QModelIndex();

			// Update Original Shape
			nif->set<int>( iStripData, "Num Strips", 1 );
			nif->set<int>( iStripData, "Has Points", 1 );

			QModelIndex iLengths = nif->getIndex( iStripData, "Strip Lengths" );
			QModelIndex iPoints = nif->getIndex( iStripData, "Points" );

			auto stripsA = strips.at(0);
			if ( iLengths.isValid() && iPoints.isValid() ) {
				nif->updateArraySize( iLengths );
				nif->set<quint16>( nif->getIndex( iLengths, 0 ), stripsA.count() );
				nif->updateArraySize( iPoints );
				nif->updateArraySize( nif->getIndex( iPoints, 0 ) );
				nif->setArray<quint16>( nif->getIndex( iPoints, 0 ), stripsA );
				nif->set<quint16>( iStripData, "Num Triangles", stripsA.count() - 2 );
			}

			// Update New Shape
			nif->set<int>( iStrip2Data, "Num Strips", 1 );
			nif->set<int>( iStrip2Data, "Has Points", 1 );

			iLengths = nif->getIndex( iStrip2Data, "Strip Lengths" );
			iPoints = nif->getIndex( iStrip2Data, "Points" );

			auto stripsB = strips.at(1);
			if ( iLengths.isValid() && iPoints.isValid() ) {
				nif->updateArraySize( iLengths );
				nif->set<quint16>( nif->getIndex( iLengths, 0 ), stripsB.count() );
				nif->updateArraySize( iPoints );
				nif->updateArraySize( nif->getIndex( iPoints, 0 ) );
				nif->setArray<quint16>( nif->getIndex( iPoints, 0 ), stripsB );
				nif->set<quint16>( iStrip2Data, "Num Triangles", stripsB.count() - 2 );
			}
		}

		return idx;
	}
};

REGISTER_SPELL( spStrippify )


class spStrippifyAll final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Stripify all TriShapes" ); }
	QString page() const override final { return Spell::tr( "Optimize" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		return nif && nif->getBSVersion() < 130 && nif->checkVersion( 0x0a000000, 0 ) && !index.isValid();
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & ) override final
	{
		QList<QPersistentModelIndex> iTriShapes;

		for ( int l = 0; l < nif->getBlockCount(); l++ ) {
			QModelIndex idx = nif->getBlockIndex( l, "NiTriShape" );

			if ( idx.isValid() )
				iTriShapes << idx;
		}

		spStrippify Stripper;

		for ( const QPersistentModelIndex& idx : iTriShapes ) {
			Stripper.castIfApplicable( nif, idx );
		}

		return QModelIndex();
	}
};

REGISTER_SPELL( spStrippifyAll )


class spTriangulate final : public Spell
{
	QString name() const override final { return Spell::tr( "Triangulate" ); }
	QString page() const override final { return Spell::tr( "Mesh" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		return nif->isNiBlock( index, "NiTriStrips" );
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
	{
		QPersistentModelIndex idx = index;
		QPersistentModelIndex iStripData = nif->getBlockIndex( nif->getLink( idx, "Data" ), "NiTriStripsData" );

		if ( !iStripData.isValid() )
			return idx;

		QVector<QVector<quint16> > strips;

		QModelIndex iPoints = nif->getIndex( iStripData, "Points" );

		if ( !iPoints.isValid() )
			return idx;

		for ( int s = 0; s < nif->rowCount( iPoints ); s++ ) {
			QVector<quint16> strip;
			QModelIndex iStrip = nif->getIndex( iPoints, s );

			for ( int p = 0; p < nif->rowCount( iStrip ); p++ )
				strip.append( nif->get<int>( nif->getIndex( iStrip, p ) ) );

			strips.append( strip );
		}

		QVector<Triangle> triangles = triangulate( strips );

		nif->insertNiBlock( "NiTriShapeData", nif->getBlockNumber( idx ) + 1 );
		QModelIndex iTriData = nif->getBlockIndex( nif->getBlockNumber( idx ) + 1, "NiTriShapeData" );

		if ( iTriData.isValid() ) {
			copyValue<int>( nif, iTriData, iStripData, "Num Vertices" );

			nif->set<int>( iTriData, "Has Vertices", 1 );
			copyArray<Vector3>( nif, iTriData, iStripData, "Vertices" );

			copyValue<int>( nif, iTriData, iStripData, "Has Normals" );
			copyArray<Vector3>( nif, iTriData, iStripData, "Normals" );

#if 0
			copyValue<int>( nif, iTriData, iStripData, "TSpace Flag" );
#endif

			copyValue<int>( nif, iTriData, iStripData, "Has Vertex Colors" );
			copyArray<Color4>( nif, iTriData, iStripData, "Vertex Colors" );

			if ( nif->getVersionNumber() <= 0x04000002 )
				copyValue<int>( nif, iTriData, iStripData, "Has UV" );
			if ( !( nif->getVersionNumber() == 0x14020007 && nif->getBSVersion() > 0 ) )
				copyValue<int>( nif, iTriData, iStripData, "Data Flags" );
			else
				copyValue<int>( nif, iTriData, iStripData, "BS Data Flags" );
			copyArray<Vector3>( nif, iTriData, iStripData, "Bitangents" );
			copyArray<Vector3>( nif, iTriData, iStripData, "Tangents" );

			QModelIndex iDstUV = nif->getIndex( iTriData, "UV Sets" );
			QModelIndex iSrcUV = nif->getIndex( iStripData, "UV Sets" );

			if ( iDstUV.isValid() && iSrcUV.isValid() ) {
				nif->updateArraySize( iDstUV );

				for ( int r = 0; r < nif->rowCount( iDstUV ); r++ ) {
					copyArray<Vector2>( nif, nif->getIndex( iDstUV, r ), nif->getIndex( iSrcUV, r ) );
				}
			}

			if ( nif->getVersionNumber() >= 0x0A000100 )
				copyValue<quint16>( nif, iTriData, iStripData, "Consistency Flags" );

			auto bound = BoundSphere( nif, iTriData );
			bound.update( nif, iStripData );

			nif->set<int>( iTriData, "Num Triangles", triangles.count() );
			nif->set<int>( iTriData, "Num Triangle Points", triangles.count() * 3 );
			nif->set<int>( iTriData, "Has Triangles", 1 );

			QModelIndex iTriangles = nif->getIndex( iTriData, "Triangles" );

			if ( iTriangles.isValid() ) {
				nif->updateArraySize( iTriangles );
				nif->setArray<Triangle>( iTriangles, triangles );
			}

			nif->setData( idx.sibling( idx.row(), NifModel::NameCol ), "NiTriShape" );
			int lnk = nif->getLink( idx, "Data" );
			nif->setLink( idx, "Data", nif->getBlockNumber( iTriData ) );
			nif->removeNiBlock( lnk );
		}

		return idx;
	}
};

REGISTER_SPELL( spTriangulate )


class spTriangulateAll final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Triangulate All Strips" ); }
	QString page() const override final { return Spell::tr( "Batch" ); }

	bool isApplicable( [[maybe_unused]] const NifModel * nif, const QModelIndex & index ) override final
	{
		return nif && nif->getBSVersion() < 130 && !index.isValid() && nif->getBlockCount() > 0;
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & ) override final
	{
		QList<QPersistentModelIndex> triStrips;

		for ( int l = 0; l < nif->getBlockCount(); l++ ) {
			QModelIndex idx = nif->getBlockIndex( l, "NiTriStrips" );
			if ( idx.isValid() )
				triStrips << idx;
		}

		spTriangulate tri;
		for ( const QPersistentModelIndex& idx : triStrips )
			tri.castIfApplicable( nif, idx );

		return QModelIndex();
	}
};

REGISTER_SPELL( spTriangulateAll )


class spStitchStrips final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Stitch Strips" ); }
	QString page() const override final { return Spell::tr( "Mesh" ); }

	static QModelIndex getStripsData( const NifModel * nif, const QModelIndex & index )
	{
		if ( nif->isNiBlock( index, "NiTriStrips" ) )
			return nif->getBlockIndex( nif->getLink( index, "Data" ), "NiTriStripsData" );

		return nif->getBlockIndex( index, "NiTriStripsData" );
	}

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		QModelIndex iData = getStripsData( nif, index );
		return iData.isValid() && nif->get<int>( iData, "Num Strips" ) > 1;
	}

	static void stitchStrips( NifModel * nif, const QModelIndex & index, bool unstitchMode = false );

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
	{
		stitchStrips( nif, index );
		return index;
	}
};

void spStitchStrips::stitchStrips( NifModel * nif, const QModelIndex & index, bool unstitchMode )
{
	QModelIndex iData = getStripsData( nif, index );
	QModelIndex iLength = nif->getIndex( iData, "Strip Lengths" );
	QModelIndex iPoints = nif->getIndex( iData, "Points" );

	if ( !( iLength.isValid() && iPoints.isValid() ) )
		return;

	QVector< QVector<quint16> > strips;

	for ( int r = 0; r < nif->rowCount( iPoints ); r++ )
		strips += nif->getArray<quint16>( nif->getIndex( iPoints, r ) );

	if ( strips.isEmpty() )
		return;

	QVector<quint16> stripLengths;
	size_t numTriangles = 0;
	{
		std::vector< unsigned int > triangleIndices;
		size_t maxVertex = 0;
		{
			QVector<Triangle> triangles = triangulate( strips );
			strips.clear();
			triangleIndices.resize( size_t( triangles.size() ) * 3 );
			unsigned int * p = triangleIndices.data();
			for ( const Triangle & t : triangles ) {
				maxVertex = std::max< size_t >( maxVertex, std::max( std::max( t[0], t[1] ), t[2] ) );
				p[0] = t[0];
				p[1] = t[1];
				p[2] = t[2];
				p = p + 3;
			}
		}
		std::vector< unsigned int > stripIndices( meshopt_stripifyBound( triangleIndices.size() ) );
		stripIndices.resize( meshopt_stripify( stripIndices.data(), triangleIndices.data(), triangleIndices.size(),
												maxVertex + 1, 0u - (unsigned int) unstitchMode ) );
		size_t startPos = 0;
		for ( const unsigned int & v : stripIndices ) {
			if ( v == ( 0u - 1u ) || &v == &( stripIndices.back() ) ) {
				size_t i = size_t( &v - stripIndices.data() );
				size_t n = i + size_t( v != ( 0u - 1u ) ) - startPos;
				if ( n > 65535 ) {
					n = 65535;
					Message::append( nullptr, "Stitch/Unstitch error",
										"Strip length is out of range", QMessageBox::Critical );
				}
				if ( n > 0 ) {
					if ( n >= 3 )
						numTriangles += n - 2;
					stripLengths.append( quint16( n ) );
					strips.append( QVector<quint16>() );
					strips.last().resize( qsizetype( n ) );
					quint16 * p = strips.last().data();
					for ( ; n > 0; n--, p++, startPos++ )
						*p = quint16( stripIndices[startPos] );
				}
				startPos = i + 1;
			}
		}
	}

	nif->set<quint16>( iData, "Num Triangles", quint16( numTriangles ) );
	nif->set<quint16>( iData, "Num Strips", quint16( stripLengths.size() ) );
	nif->updateArraySize( iLength );
	nif->setArray<quint16>( iLength, stripLengths );
	nif->updateArraySize( iPoints );
	for ( const auto & s : strips ) {
		int i = int( &s - strips.constData() );
		nif->updateArraySize( nif->getIndex( iPoints, i ) );
		nif->setArray<quint16>( nif->getIndex( iPoints, i ), s );
	}
}

REGISTER_SPELL( spStitchStrips )


class spUnstitchStrips final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Unstitch Strips" ); }
	QString page() const override final { return Spell::tr( "Mesh" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		QModelIndex iData = spStitchStrips::getStripsData( nif, index );
		return iData.isValid() && nif->get<int>( iData, "Num Strips" ) == 1;
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final
	{
		spStitchStrips::stitchStrips( nif, index, true );
		return index;
	}
};

REGISTER_SPELL( spUnstitchStrips )

