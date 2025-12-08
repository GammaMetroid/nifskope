#include "coacd.h"

#include <QLibrary>
#include <QString>
#include <cstdlib>
#include <cstring>

std::vector< CoACD::Mesh > CoACD::processMesh( const Mesh & m )
{
	std::vector< Mesh >	coacdOutput;

	QLibrary	coacdLib( QLatin1StringView("_coacd") );
	if ( coacdLib.load() && !( m.vertices.empty() || m.indices.empty() ) ) {
		fnSetLogLevel	setLogLevel = fnSetLogLevel( coacdLib.resolve( "CoACD_setLogLevel" ) );
		fnRun	coacdRun = fnRun( coacdLib.resolve( "CoACD_run" ) );
		fnFreeMeshArray	freeMeshArray = fnFreeMeshArray( coacdLib.resolve( "CoACD_freeMeshArray" ) );
		if ( coacdRun && freeMeshArray ) {
			if ( setLogLevel )
				setLogLevel( "error" );
			MeshDataC	tmp;
			tmp.vertices = const_cast< double * >( &( m.vertices.front()[0] ) );
			tmp.numVerts = std::uint64_t( m.vertices.size() );
			tmp.indices = const_cast< int * >( &( m.indices.front()[0] ) );
			tmp.numTriangles = std::uint64_t( m.indices.size() );
			MeshArrayC	tmp2 = coacdRun( &tmp, threshold, maxConvexHull, preprocessMode, prepResolution,
											sampleResolution, mctsNodes, mctsIteration, mctsMaxDepth, pca,
											merge, decimate, maxCHVertex, extrude, extrudeMargin, apxMode,
											(unsigned int) seed );
			if ( tmp2.meshes && tmp2.numMeshes ) {
				coacdOutput.resize( size_t( tmp2.numMeshes ) );
				for ( size_t i = 0; i < coacdOutput.size(); i++ ) {
					size_t	n = size_t( tmp2.meshes[i].numVerts );
					coacdOutput[i].vertices.resize( n );
					std::memcpy( coacdOutput[i].vertices.data(), tmp2.meshes[i].vertices, n * sizeof( double ) * 3 );
					n = size_t( tmp2.meshes[i].numTriangles );
					coacdOutput[i].indices.resize( n );
					std::memcpy( coacdOutput[i].indices.data(), tmp2.meshes[i].indices, n * sizeof( int ) * 3 );
				}
			}
			freeMeshArray( tmp2 );
		}
	}

	return coacdOutput;
}

void CoACD::loadSettings()
{
	// TODO
}

void CoACD::saveSettings()
{
	// TODO
}
