#pragma once
#include "RaytracingHlslCompat.h"
#include "CheckCast.h"

class ObjLoader
{
	class Object
	{
		std::string m_name;

	public:
		Object(std::string const& name)
		: m_name(name)
		{}

		std::string const& GetName() const { return m_name; }

		void AddVertex(XMFLOAT3 const& vertex)
		{
			m_vertices.push_back(vertex);
		}

		void AddNormal(XMFLOAT3 const& normal)
		{
			m_normals.push_back(normal);
		}

		struct ThreeIndices
		{
			unsigned int Values[3];
		};

		void AddFace(ThreeIndices const& vertexIndices)
		{
			Face face;
			face.UseNormals = false;
			face.VertexIndices = vertexIndices;
			m_faces.push_back(face);
		}

		void AddFace(ThreeIndices const& vertexIndices, ThreeIndices const& normalIndices)
		{
			Face face;
			face.UseNormals = true;
			face.VertexIndices = vertexIndices;
			face.NormalIndices = normalIndices;
			m_faces.push_back(face);
		}

		std::vector<XMFLOAT3> m_vertices;
		std::vector<XMFLOAT3> m_normals;

		struct Face
		{
			ThreeIndices VertexIndices;

			bool UseNormals;
			ThreeIndices NormalIndices;
		};
		std::vector<Face> m_faces;
	};

	std::vector<Object> m_objects;

public:
	void Load(wchar_t const* fileName);

	void GetObjectVerticesAndIndices(
		std::string const& name,
		float scale,
		std::vector<Vertex>* vertices,
		std::vector<Index>* indices);

	void GetCubeVerticesAndIndices(
		float xScale,
		float yScale,
		float zScale,
		float xTranslate,
		float yTranslate,
		float zTranslate,
		float uvScale,
		std::vector<Vertex>* vertices,
		std::vector<Index>* indices);

private:
	Object* GetObject(std::string const& name);
	Object* GetOrCreateObject(std::string const& name);
	XMUINT2 GetVertexAndNormalIndex(std::string const& token);
};