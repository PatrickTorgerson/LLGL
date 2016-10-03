/*
 * GLExtensionViewer.h
 * 
 * This file is part of the "LLGL" project (Copyright (c) 2015 by Lukas Hermanns)
 * See "LICENSE.txt" for license information.
 */

#ifndef __LLGL_GL_EXTENSION_VIEWER_H__
#define __LLGL_GL_EXTENSION_VIEWER_H__


#include <string>
#include <map>


namespace LLGL
{


//! OpenGL extension map type.
using OpenGLExtensionMap = std::map<std::string, bool>;

class GLExtensionViewer
{

    public:

        GLExtensionViewer(OpenGLExtensionMap&& extensions);

        //! Returns true if the specified extensions is supported.
        bool HasExtension(const std::string& name) const;

        //! Returns the hash-map of all extensions.
        inline const OpenGLExtensionMap& GetExtensions() const
        {
            return extensions_;
        }

    private:

        OpenGLExtensionMap extensions_;

};


} // /namespace LLGL


#endif



// ================================================================================
