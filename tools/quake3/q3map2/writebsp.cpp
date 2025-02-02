/* -------------------------------------------------------------------------------

   Copyright (C) 1999-2007 id Software, Inc. and contributors.
   For a list of contributors, see the accompanying CONTRIBUTORS file.

   This file is part of GtkRadiant.

   GtkRadiant is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   GtkRadiant is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GtkRadiant; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

   ----------------------------------------------------------------------------------

   This code has been altered significantly from its original form, to support
   several games based on the Quake III Arena engine, in the form of "Q3Map2."

   ------------------------------------------------------------------------------- */



/* dependencies */
#include "q3map2.h"



/*
   EmitShader()
   emits a bsp shader entry
 */

int EmitShader( const char *shader, const int *contentFlags, const int *surfaceFlags ){
	int i;
	shaderInfo_t    *si;


	/* handle special cases */
	if ( shader == NULL ) {
		shader = "noshader";
	}

	/* try to find an existing shader */
	for ( i = 0; i < numBSPShaders; i++ )
	{
		/* ydnar: handle custom surface/content flags */
		if ( surfaceFlags != NULL && bspShaders[ i ].surfaceFlags != *surfaceFlags ) {
			continue;
		}
		if ( contentFlags != NULL && bspShaders[ i ].contentFlags != *contentFlags ) {
			continue;
		}
		if ( !doingBSP ){
			si = ShaderInfoForShader( shader );
			if ( !strEmptyOrNull( si->remapShader ) ) {
				shader = si->remapShader;
			}
		}
		/* compare name */
		if ( striEqual( shader, bspShaders[ i ].shader ) ) {
			return i;
		}
	}

	// i == numBSPShaders

	/* get shaderinfo */
	si = ShaderInfoForShader( shader );

	/* emit a new shader */
	AUTOEXPAND_BY_REALLOC_BSP( Shaders, 1024 );

	numBSPShaders++;
	// copy and clear the rest of memory
	strncpy( bspShaders[ i ].shader, si->shader, sizeof( bspShaders[ i ].shader ) );
	bspShaders[ i ].surfaceFlags = si->surfaceFlags;
	bspShaders[ i ].contentFlags = si->contentFlags;

	/* handle custom content/surface flags */
	if ( surfaceFlags != NULL ) {
		bspShaders[ i ].surfaceFlags = *surfaceFlags;
	}
	if ( contentFlags != NULL ) {
		bspShaders[ i ].contentFlags = *contentFlags;
	}

	/* recursively emit any damage shaders */
	if ( !strEmptyOrNull( si->damageShader ) ) {
		Sys_FPrintf( SYS_VRB, "Shader %s has damage shader %s\n", si->shader.c_str(), si->damageShader );
		EmitShader( si->damageShader, NULL, NULL );
	}

	/* return it */
	return i;
}



/*
   EmitPlanes()
   there is no opportunity to discard planes, because all of the original
   brushes will be saved in the map
 */

void EmitPlanes( void ){
	/* walk plane list */
	for ( size_t i = 0; i < mapplanes.size(); ++i )
	{
		AUTOEXPAND_BY_REALLOC_BSP( Planes, 1024 );
		bspPlanes[ numBSPPlanes ] = mapplanes[i].plane;
		numBSPPlanes++;
	}

	/* emit some statistics */
	Sys_FPrintf( SYS_VRB, "%9d BSP planes\n", numBSPPlanes );
}



/*
   EmitLeaf()
   emits a leafnode to the bsp file
 */

void EmitLeaf( node_t *node ){
	bspLeaf_t       *leaf_p;
	drawSurfRef_t   *dsr;


	/* check limits */
	if ( numBSPLeafs >= MAX_MAP_LEAFS ) {
		Error( "MAX_MAP_LEAFS" );
	}

	leaf_p = &bspLeafs[numBSPLeafs];
	numBSPLeafs++;

	leaf_p->cluster = node->cluster;
	leaf_p->area = node->area;

	/* emit bounding box */
	leaf_p->minmax.maxs = node->minmax.maxs;
	leaf_p->minmax.mins = node->minmax.mins;

	/* emit leaf brushes */
	leaf_p->firstBSPLeafBrush = numBSPLeafBrushes;
	for ( const brush_t& b : node->brushlist )
	{
		/* something is corrupting brushes */
		// if ( (size_t) b < 256 ) {
		// 	Sys_Warning( "Node brush list corrupted (0x%08X)\n", b );
		// 	break;
		// }
		//%	if( b->guard != 0xDEADBEEF )
		//%		Sys_Printf( "Brush %6d: 0x%08X Guard: 0x%08X Next: 0x%08X Original: 0x%08X Sides: %d\n", b->brushNum, b, b, b->next, b->original, b->numsides );

		AUTOEXPAND_BY_REALLOC_BSP( LeafBrushes, 1024 );
		bspLeafBrushes[ numBSPLeafBrushes ] = b.original->outputNum;
		numBSPLeafBrushes++;
	}

	leaf_p->numBSPLeafBrushes = numBSPLeafBrushes - leaf_p->firstBSPLeafBrush;

	/* emit leaf surfaces */
	if ( node->opaque ) {
		return;
	}

	/* add the drawSurfRef_t drawsurfs */
	leaf_p->firstBSPLeafSurface = numBSPLeafSurfaces;
	for ( dsr = node->drawSurfReferences; dsr; dsr = dsr->nextRef )
	{
		AUTOEXPAND_BY_REALLOC_BSP( LeafSurfaces, 1024 );
		bspLeafSurfaces[ numBSPLeafSurfaces ] = dsr->outputNum;
		numBSPLeafSurfaces++;
	}

	leaf_p->numBSPLeafSurfaces = numBSPLeafSurfaces - leaf_p->firstBSPLeafSurface;
}


/*
   EmitDrawNode_r()
   recursively emit the bsp nodes
 */

int EmitDrawNode_r( node_t *node ){
	bspNode_t   *n;
	int i, n0;


	/* check for leafnode */
	if ( node->planenum == PLANENUM_LEAF ) {
		EmitLeaf( node );
		return -numBSPLeafs;
	}

	/* emit a node */
	AUTOEXPAND_BY_REALLOC_BSP( Nodes, 1024 );
	n0 = numBSPNodes;
	n = &bspNodes[ n0 ];
	numBSPNodes++;

	n->minmax.mins = node->minmax.mins;
	n->minmax.maxs = node->minmax.maxs;

	if ( node->planenum & 1 ) {
		Error( "WriteDrawNodes_r: odd planenum" );
	}
	n->planeNum = node->planenum;

	//
	// recursively output the other nodes
	//
	for ( i = 0; i < 2; i++ )
	{
		if ( node->children[i]->planenum == PLANENUM_LEAF ) {
			n->children[i] = -( numBSPLeafs + 1 );
			EmitLeaf( node->children[i] );
		}
		else
		{
			n->children[i] = numBSPNodes;
			EmitDrawNode_r( node->children[i] );
			// n may have become invalid here, so...
			n = &bspNodes[ n0 ];
		}
	}

	return n - bspNodes;
}



/*
   ============
   SetModelNumbers
   ============
 */
void SetModelNumbers( void ){
	int models = 1;
	for ( std::size_t i = 1; i < entities.size(); ++i ) {
		if ( !entities[i].brushes.empty() || entities[i].patches ) {
			char value[16];
			sprintf( value, "*%i", models );
			models++;
			entities[i].setKeyValue( "model", value );
		}
	}

}




/*
   SetLightStyles()
   sets style keys for entity lights
 */

void SetLightStyles( void ){
	int j, numStyles;
	char value[ 10 ];
	char lightTargets[ MAX_SWITCHED_LIGHTS ][ 64 ];
	int lightStyles[ MAX_SWITCHED_LIGHTS ];

	/* -keeplights option: force lights to be kept and ignore what the map file says */
	if ( keepLights ) {
		entities[0].setKeyValue( "_keepLights", "1" );
	}

	/* ydnar: determine if we keep lights in the bsp */
	entities[ 0 ].read_keyvalue( keepLights, "_keepLights" );

	/* any light that is controlled (has a targetname) must have a unique style number generated for it */
	numStyles = 0;
	for ( std::size_t i = 1; i < entities.size(); ++i )
	{
		entity_t& e = entities[ i ];

		if ( !e.classname_prefixed( "light" ) ) {
			continue;
		}
		const char *t;
		if ( !e.read_keyvalue( t, "targetname" ) ) {
			/* ydnar: strip the light from the BSP file */
			if ( !keepLights ) {
				e.epairs.clear();
				numStrippedLights++;
			}

			/* next light */
			continue;
		}

		/* get existing style */
		const int style = e.intForKey( "style" );
		if ( style < LS_NORMAL || style > LS_NONE ) {
			Error( "Invalid lightstyle (%d) on entity %zu", style, i );
		}

		/* find this targetname */
		for ( j = 0; j < numStyles; j++ )
			if ( lightStyles[ j ] == style && strEqual( lightTargets[ j ], t ) ) {
				break;
			}

		/* add a new style */
		if ( j >= numStyles ) {
			if ( numStyles == MAX_SWITCHED_LIGHTS ) {
				Error( "MAX_SWITCHED_LIGHTS (%d) exceeded, reduce the number of lights with targetnames", MAX_SWITCHED_LIGHTS );
			}
			strcpy( lightTargets[ j ], t );
			lightStyles[ j ] = style;
			numStyles++;
		}

		/* set explicit style */
		sprintf( value, "%d", 32 + j );
		e.setKeyValue( "style", value );

		/* set old style */
		if ( style != LS_NORMAL ) {
			sprintf( value, "%d", style );
			e.setKeyValue( "switch_style", value );
		}
	}

	/* emit some statistics */
	Sys_FPrintf( SYS_VRB, "%9d light entities stripped\n", numStrippedLights );
}



/*
   BeginBSPFile()
   starts a new bsp file
 */

void BeginBSPFile( void ){
	/* these values may actually be initialized if the file existed when loaded, so clear them explicitly */
	numBSPModels = 0;
	numBSPNodes = 0;
	numBSPBrushSides = 0;
	numBSPLeafSurfaces = 0;
	numBSPLeafBrushes = 0;

	/* leave leaf 0 as an error, because leafs are referenced as negative number nodes */
	numBSPLeafs = 1;


	/* ydnar: gs mods: set the first 6 drawindexes to 0 1 2 2 1 3 for triangles and quads */
	numBSPDrawIndexes = 6;
	AUTOEXPAND_BY_REALLOC_BSP( DrawIndexes, 1024 );
	bspDrawIndexes[ 0 ] = 0;
	bspDrawIndexes[ 1 ] = 1;
	bspDrawIndexes[ 2 ] = 2;
	bspDrawIndexes[ 3 ] = 0;
	bspDrawIndexes[ 4 ] = 2;
	bspDrawIndexes[ 5 ] = 3;
}



/*
   EndBSPFile()
   finishes a new bsp and writes to disk
 */

void EndBSPFile( bool do_write ){
	Sys_FPrintf( SYS_VRB, "--- EndBSPFile ---\n" );

	EmitPlanes();

	numBSPEntities = entities.size();
	UnparseEntities();

	if ( do_write ) {
		/* write the surface extra file */
		WriteSurfaceExtraFile( source );

		/* write the bsp */
		auto path = StringOutputStream( 256 )( source, ".bsp" );
		Sys_Printf( "Writing %s\n", path.c_str() );
		WriteBSPFile( path );
	}
}



/*
   EmitBrushes()
   writes the brush list to the bsp
 */

void EmitBrushes( brushlist_t& brushes, int *firstBrush, int *numBrushes ){
	bspBrush_t      *db;
	bspBrushSide_t  *cp;


	/* set initial brush */
	if ( firstBrush != NULL ) {
		*firstBrush = numBSPBrushes;
	}
	if ( numBrushes != NULL ) {
		*numBrushes = 0;
	}

	/* walk list of brushes */
	for ( brush_t& b : brushes )
	{
		/* check limits */
		AUTOEXPAND_BY_REALLOC_BSP( Brushes, 1024 );

		/* get bsp brush */
		b.outputNum = numBSPBrushes;
		db = &bspBrushes[ numBSPBrushes ];
		numBSPBrushes++;
		if ( numBrushes != NULL ) {
			( *numBrushes )++;
		}

		db->shaderNum = EmitShader( b.contentShader->shader, &b.contentShader->contentFlags, &b.contentShader->surfaceFlags );
		db->firstSide = numBSPBrushSides;

		/* walk sides */
		db->numSides = 0;
		for ( side_t& side : b.sides )
		{
			/* set output number to bogus initially */
			side.outputNum = -1;

			/* check count */
			AUTOEXPAND_BY_REALLOC_BSP( BrushSides, 1024 );

			/* emit side */
			side.outputNum = numBSPBrushSides;
			cp = &bspBrushSides[ numBSPBrushSides ];
			db->numSides++;
			numBSPBrushSides++;
			cp->planeNum = side.planenum;

			/* emit shader */
			if ( side.shaderInfo ) {
				cp->shaderNum = EmitShader( side.shaderInfo->shader, &side.shaderInfo->contentFlags, &side.shaderInfo->surfaceFlags );
			}
			else if( side.bevel ) { /* emit surfaceFlags for bevels to get correct physics at walkable brush edges and vertices */
				cp->shaderNum = EmitShader( NULL, NULL, &side.surfaceFlags );
			}
			else{
				cp->shaderNum = EmitShader( NULL, NULL, NULL );
			}
		}
	}
}



/*
   EmitFogs() - ydnar
   turns map fogs into bsp fogs
 */

void EmitFogs( void ){
	int i, j;


	/* setup */
	numBSPFogs = numMapFogs;

	/* walk list */
	for ( i = 0; i < numMapFogs; i++ )
	{
		/* set shader */
		// copy and clear the rest of memory
		strncpy( bspFogs[ i ].shader, mapFogs[ i ].si->shader, sizeof( bspFogs[ i ].shader ) );

		/* global fog doesn't have an associated brush */
		if ( mapFogs[ i ].brush == NULL ) {
			bspFogs[ i ].brushNum = -1;
			bspFogs[ i ].visibleSide = -1;
		}
		else
		{
			/* set brush */
			bspFogs[ i ].brushNum = mapFogs[ i ].brush->outputNum;

			/* try to use forced visible side */
			if ( mapFogs[ i ].visibleSide >= 0 ) {
				bspFogs[ i ].visibleSide = mapFogs[ i ].visibleSide;
				continue;
			}

			/* find visible side */
			for ( j = 0; j < 6; j++ )
			{
				if ( !mapFogs[ i ].brush->sides[ j ].visibleHull.empty() ) {
					Sys_Printf( "Fog %d has visible side %d\n", i, j );
					bspFogs[ i ].visibleSide = j;
					break;
				}
			}
		}
	}
}



/*
   BeginModel()
   sets up a new brush model
 */

void BeginModel( void ){
	MinMax minmax;
	MinMax lgMinmax;          /* ydnar: lightgrid mins/maxs */

	/* test limits */
	AUTOEXPAND_BY_REALLOC_BSP( Models, 256 );

	/* get model and entity */
	bspModel_t *mod = &bspModels[ numBSPModels ];
	const entity_t& e = entities[ mapEntityNum ];

	/* bound the brushes */
	for ( const brush_t& b : e.brushes )
	{
		/* ignore non-real brushes (origin, etc) */
		if ( b.sides.empty() ) {
			continue;
		}
		minmax.extend( b.minmax );

		/* ydnar: lightgrid bounds */
		if ( b.compileFlags & C_LIGHTGRID ) {
			lgMinmax.extend( b.minmax );
		}
	}

	/* bound patches */
	for ( const parseMesh_t *p = e.patches; p; p = p->next )
	{
		for ( int i = 0; i < ( p->mesh.width * p->mesh.height ); i++ )
			minmax.extend( p->mesh.verts[i].xyz );
	}

	/* ydnar: lightgrid mins/maxs */
	if ( lgMinmax.valid() ) {
		/* use lightgrid bounds */
		mod->minmax = lgMinmax;
	}
	else
	{
		/* use brush/patch bounds */
		mod->minmax = minmax;
	}

	/* note size */
	Sys_FPrintf( SYS_VRB, "BSP bounds: { %f %f %f } { %f %f %f }\n", minmax.mins[0], minmax.mins[1], minmax.mins[2], minmax.maxs[0], minmax.maxs[1], minmax.maxs[2] );
	if ( lgMinmax.valid() )
		Sys_FPrintf( SYS_VRB, "Lightgrid bounds: { %f %f %f } { %f %f %f }\n", lgMinmax.mins[0], lgMinmax.mins[1], lgMinmax.mins[2], lgMinmax.maxs[0], lgMinmax.maxs[1], lgMinmax.maxs[2] );

	/* set firsts */
	mod->firstBSPSurface = numBSPDrawSurfaces;
	mod->firstBSPBrush = numBSPBrushes;
}




/*
   EndModel()
   finish a model's processing
 */

void EndModel( entity_t *e, node_t *headnode ){
	bspModel_t  *mod;


	/* note it */
	Sys_FPrintf( SYS_VRB, "--- EndModel ---\n" );

	/* emit the bsp */
	mod = &bspModels[ numBSPModels ];
	EmitDrawNode_r( headnode );

	/* set surfaces and brushes */
	mod->numBSPSurfaces = numBSPDrawSurfaces - mod->firstBSPSurface;
	mod->firstBSPBrush = e->firstBrush;
	mod->numBSPBrushes = e->numBrushes;

	/* increment model count */
	numBSPModels++;
}
