/*
-----------------------------------------------------------------------------
This source file is part of OGRE
(Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org/

Copyright (c) 2000-2011 Torus Knot Software Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/
#ifndef __GLSLLinkProgram_H__
#define __GLSLLinkProgram_H__

#include "CmGLPrerequisites.h"
#include "CmGpuProgram.h"
#include "CmHardwareVertexBuffer.h"

namespace CamelotEngine {
	/// structure used to keep track of named uniforms in the linked program object
	struct GLUniformReference
	{
		/// GL location handle
		GLint  mLocation;
		/// Which type of program params will this value come from?
		GpuProgramType mSourceProgType;
		/// The constant definition it relates to
		const GpuConstantDefinition* mConstantDef;
	};

	typedef vector<GLUniformReference>::type GLUniformReferenceList;
	typedef GLUniformReferenceList::iterator GLUniformReferenceIterator;

	/** C++ encapsulation of GLSL Program Object

	*/

	class CM_RSGL_EXPORT GLSLLinkProgram
	{
	private:
		/// container of uniform references that are active in the program object
		GLUniformReferenceList mGLUniformReferences;

		/// Linked vertex program
		GLSLGpuProgram* mVertexProgram;
		/// Linked geometry program
		GLSLGpuProgram* mGeometryProgram;
		/// Linked fragment program
		GLSLGpuProgram* mFragmentProgram;

		/// flag to indicate that uniform references have already been built
		bool		mUniformRefsBuilt;
		/// GL handle for the program object
		GLhandleARB mGLHandle;
		/// flag indicating that the program object has been successfully linked
		GLint		mLinked;

		/// build uniform references from active named uniforms
		void buildGLUniformReferences(void);
		/// extract attributes
		void extractAttributes(void);

		typedef set<GLuint>::type AttributeSet;
		// Custom attribute bindings
		AttributeSet mValidAttributes;

		/// Name / attribute list
		struct CustomAttribute
		{
			String name;
			GLuint attrib;
			CustomAttribute(const String& _name, GLuint _attrib)
				:name(_name), attrib(_attrib) {}
		};

		static CustomAttribute msCustomAttributes[];

	public:
		/// constructor should only be used by GLSLLinkProgramManager
		GLSLLinkProgram(GLSLGpuProgram* vertexProgram, GLSLGpuProgram* geometryProgram, GLSLGpuProgram* fragmentProgram);
		~GLSLLinkProgram(void);

		/** Makes a program object active by making sure it is linked and then putting it in use.

		*/
		void activate(void);
		/** updates program object uniforms using data from GpuProgramParamters.
		normally called by GLSLGpuProgram::bindParameters() just before rendering occurs.
		*/
		void updateUniforms(GpuProgramParametersSharedPtr params, UINT16 mask, GpuProgramType fromProgType);
		/// get the GL Handle for the program object
		GLhandleARB getGLHandle(void) const { return mGLHandle; }

		/// Get the index of a non-standard attribute bound in the linked code
		GLuint getAttributeIndex(VertexElementSemantic semantic, UINT32 index);
		/// Is a non-standard attribute bound in the linked code?
		bool isAttributeValid(VertexElementSemantic semantic, UINT32 index);
	};

}

#endif // __GLSLLinkProgram_H__
