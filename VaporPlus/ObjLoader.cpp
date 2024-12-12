#include "stdafx.h"
#include "ObjLoader.h"

void ObjLoader::Load(wchar_t const* fileName)
{
	std::ifstream fileStream(fileName);
	std::string line;
	
	Object* currentObject = nullptr;

	if (!fileStream.good())
	{
		ThrowIfFailed(E_FAIL); // Couldn't find the mesh file
	}

	while (fileStream.good())
	{
		std::getline(fileStream, line);

		if (line.length() == 0)
			continue;

		std::string objectIdentifierPrefix = "# object ";
		if (line.find(objectIdentifierPrefix) == 0)
		{
			currentObject = GetOrCreateObject(line.substr(objectIdentifierPrefix.length()));

		}
		else if (line[0] == 'v' && line[1] == ' ')
		{
			XMFLOAT3 vertex;
			std::stringstream stringStream(line.substr(1));
			stringStream >> vertex.x >> vertex.y >> vertex.z;
			currentObject->AddVertex(vertex);
		}
		else if (line[0] == 'v' && line[1] == 'n'&& line[2] == ' ')
		{
			XMFLOAT3 normal;
			std::stringstream stringStream(line.substr(2));
			stringStream >> normal.x >> normal.y >> normal.z;
			currentObject->AddNormal(normal);
		}
		else if (line[0] == 'g' && line[1] == ' ')
		{
			std::string objectName = line.substr(2);
			currentObject = GetOrCreateObject(objectName);
		}
		else if (line[0] == 'f' && line[1] == ' ')
		{
			size_t delim1 = line.find('/');
			size_t delim2 = line.find('/', delim1);

			bool useNormals = delim1 != -1;

			if (useNormals)
			{
				// Face info includes normals
				std::string tokens[3];
				std::stringstream stringStream(line.substr(1));
				stringStream >> tokens[0] >> tokens[1] >> tokens[2];

				XMUINT2 vertexAndNormal[3];
				for (int i = 0; i<3; ++i)
					vertexAndNormal[i] = GetVertexAndNormalIndex(tokens[i]);

				Object::ThreeIndices vertexIndices;
				for (int i = 0; i<3; ++i)
					vertexIndices.Values[i] = vertexAndNormal[i].x;

				Object::ThreeIndices normalIndices;
				for (int i = 0; i<3; ++i)
					normalIndices.Values[i] = vertexAndNormal[i].y;

				currentObject->AddFace(vertexIndices, normalIndices);
			}
			else
			{
				// No normals
				Object::ThreeIndices vertexIndices;
				std::stringstream stringStream(line.substr(1));
				stringStream >> vertexIndices.Values[0] >> vertexIndices.Values[1] >> vertexIndices.Values[2];
				currentObject->AddFace(vertexIndices);
			}
		}
	}
}

ObjLoader::Object* ObjLoader::GetObject(std::string const& name)
{
	for (size_t i = 0; i < m_objects.size(); ++i)
	{
		if (m_objects[i].GetName() == name)
			return &m_objects[i];
	}

	return nullptr;
}

ObjLoader::Object* ObjLoader::GetOrCreateObject(std::string const& name)
{
	Object* result = GetObject(name);

	if (result)
		return result;

	// Object not found; create a new one
	m_objects.push_back(Object(name));
	return &m_objects.back();
}

XMUINT2 ObjLoader::GetVertexAndNormalIndex(std::string const& token)
{
	XMUINT2 result{};

	size_t delimiterPosition1 = token.find('/');
	size_t delimiterPosition2 = token.find('/', delimiterPosition1 + 1);
	size_t delimiterLength = 1;

	{
		std::stringstream substring(token.substr(0, delimiterPosition1));
		substring >> result.x;
	}
	{
		std::stringstream substring(token.substr(delimiterPosition2 + delimiterLength));
		substring >> result.y;
	}

	return result;
}

void ObjLoader::GetObjectVerticesAndIndices(
	std::string const& name,
	float scale,
	std::vector<Vertex>* vertices,
	std::vector<Index>* indices)
{
	Object* obj = GetObject(name);
	
	for (size_t faceIndex = 0; faceIndex < obj->m_faces.size(); ++faceIndex)
	{
		for (int vertexIndex = 0; vertexIndex < 3; ++vertexIndex)
		{
			Vertex v{};

			// Get a vertex for this face
			Index index = obj->m_faces[faceIndex].VertexIndices.Values[vertexIndex] - 1; // 1-indexed
			v.position = obj->m_vertices.at(index);
			
			v.position.x *= scale;
			v.position.y *= scale;
			v.position.z *= scale; // Shrink

			// Get that vertex's normal
			if (obj->m_faces[faceIndex].UseNormals)
			{
				uint32_t normalIndex = obj->m_faces[faceIndex].NormalIndices.Values[vertexIndex] - 1;
				XMFLOAT3 n = obj->m_normals[normalIndex];
				v.normal = n;
			}

			// (No UV)
			v.uv.x = 0.5f;
			v.uv.y = 0.5f;

			assert(indices->size() < UINT16_MAX);
			indices->push_back(CheckCastIndex(vertices->size()));
			vertices->push_back(v);
		}
	}
}

void ObjLoader::GetCubeVerticesAndIndices(
	float xScale,
	float yScale,
	float zScale,
	float xTranslate,
	float yTranslate,
	float zTranslate,
	float uvScale,
	std::vector<Vertex>* vertices,
	std::vector<Index>* indices)
{
	// While the obj loader could load a cube from a file, this is fastpathed by hard-coding 
	// the data for a cube right here.

    // Cube indices.
    Index cubeIndices[] =
    {
        3,1,0,
        2,1,3,

        6,4,5,
        7,4,6,

        11,9,8,
        10,9,11,

        14,12,13,
        15,12,14,

        19,17,16,
        18,17,19,

        22,20,21,
        20,22,23
    };

	float uv = uvScale;

    // Cube vertices positions and corresponding triangle normals.
    Vertex cubeVertices[] =
    {
		// Top
        { XMFLOAT3(-1.0f, 1.0f, -1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, 0.0f) },
        { XMFLOAT3(1.0f, 1.0f, -1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f) , XMFLOAT3(uv, 0.0f, 0.0f) },
        { XMFLOAT3(1.0f, 1.0f, 1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f) , XMFLOAT3(uv, uv, 0.0f) },
        { XMFLOAT3(-1.0f, 1.0f, 1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f) , XMFLOAT3(0.0f, uv, 0.0f) },

		// Bottom
        { XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f, -1.0f, 0.0f) , XMFLOAT3(0.0f, 0.0f, 0.0f) }, // No UVs for bottom
        { XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f, -1.0f, 0.0f) , XMFLOAT3(0.0f, 0.0f, 0.0f) },
        { XMFLOAT3(1.0f, -1.0f, 1.0f), XMFLOAT3(0.0f, -1.0f, 0.0f) , XMFLOAT3(0.0f, 0.0f, 0.0f) },
        { XMFLOAT3(-1.0f, -1.0f, 1.0f), XMFLOAT3(0.0f, -1.0f, 0.0f) , XMFLOAT3(0.0f, 0.0f, 0.0f) },

		// Left
        { XMFLOAT3(-1.0f, -1.0f, 1.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f) , XMFLOAT3(0.0f, uv, 0.0f) }, // lower left
        { XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f) , XMFLOAT3(uv, uv, 0.0f) },  // lower right
        { XMFLOAT3(-1.0f, 1.0f, -1.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f) , XMFLOAT3(uv, 0, 0.0f) },     // upper right
        { XMFLOAT3(-1.0f, 1.0f, 1.0f), XMFLOAT3(-1.0f, 0.0f, 0.0f) , XMFLOAT3(0.0f, 0, 0.0f) },    // upper left

		// Right
        { XMFLOAT3(1.0f, -1.0f, 1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) , XMFLOAT3(uv, uv, 0.0f) }, // Lower right
        { XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) , XMFLOAT3(0, uv, 0.0f) },  // Lower left
        { XMFLOAT3(1.0f, 1.0f, -1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) , XMFLOAT3(0, 0, 0.0f) },     // Upper left
        { XMFLOAT3(1.0f, 1.0f, 1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) , XMFLOAT3(uv, 0, 0.0f) },    // Upper right

		// Front
        { XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f, 0.0f, -1.0f) , XMFLOAT3(0, uv, 0.0f) }, // Lower left
        { XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f, 0.0f, -1.0f) , XMFLOAT3(uv, uv, 0.0f) }, // Lower right
        { XMFLOAT3(1.0f, 1.0f, -1.0f), XMFLOAT3(0.0f, 0.0f, -1.0f) , XMFLOAT3(uv, 0, 0.0f) },   // Upper right
        { XMFLOAT3(-1.0f, 1.0f, -1.0f), XMFLOAT3(0.0f, 0.0f, -1.0f) , XMFLOAT3(0, 0, 0.0f) },   // Upper left

		// Back
        { XMFLOAT3(-1.0f, -1.0f, 1.0f), XMFLOAT3(0.0f, 0.0f, 1.0f) , XMFLOAT3(uv, uv, 0.0f) },  // 20, upper right
        { XMFLOAT3(1.0f, -1.0f, 1.0f), XMFLOAT3(0.0f, 0.0f, 1.0f) , XMFLOAT3(0, uv, 0.0f) },    // 21, upper left
        { XMFLOAT3(1.0f, 1.0f, 1.0f), XMFLOAT3(0.0f, 0.0f, 1.0f) , XMFLOAT3(0, 0, 0.0f) },      // 22, lower left
        { XMFLOAT3(-1.0f, 1.0f, 1.0f), XMFLOAT3(0.0f, 0.0f, 1.0f) , XMFLOAT3(uv, 0, 0.0f) },    // 23
    };

	Index indexBaseline = CheckCastIndex(vertices->size());

	for (int i = 0; i < ARRAYSIZE(cubeVertices); ++i)
	{
		Vertex v = cubeVertices[i];

		v.position.x *= xScale;
		v.position.x += xTranslate;

		v.position.y *= yScale;
		v.position.y += yTranslate;

		v.position.z *= zScale;
		v.position.z += zTranslate;

		vertices->push_back(v);
	}

	for (int i = 0; i < ARRAYSIZE(cubeIndices); ++i)
	{
		indices->push_back(indexBaseline + cubeIndices[i]);
	}

}