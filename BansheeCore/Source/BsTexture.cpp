#include "BsPixelBuffer.h"
#include "BsTexture.h"
#include "BsTextureRTTI.h"
#include "BsDataStream.h"
#include "BsException.h"
#include "BsDebug.h"
#include "BsCoreThread.h"
#include "BsAsyncOp.h"
#include "BsResources.h"
#include "BsPixelUtil.h"

namespace BansheeEngine 
{
	TextureProperties::TextureProperties()
		:mHeight(32), mWidth(32), mDepth(1), mNumMipmaps(0),
		mHwGamma(false), mMultisampleCount(0), mTextureType(TEX_TYPE_2D),
		mFormat(PF_UNKNOWN), mUsage(TU_DEFAULT)
	{
		mFormat = TextureManager::instance().getNativeFormat(mTextureType, mFormat, mUsage, mHwGamma);
	}

	TextureProperties::TextureProperties(TextureType textureType, UINT32 width, UINT32 height, UINT32 depth, UINT32 numMipmaps,
		PixelFormat format, int usage, bool hwGamma, UINT32 multisampleCount)
		:mHeight(height), mWidth(width), mDepth(depth), mNumMipmaps(numMipmaps),
		mHwGamma(hwGamma), mMultisampleCount(multisampleCount), mTextureType(textureType),
		mFormat(format), mUsage(usage)
	{
		mFormat = TextureManager::instance().getNativeFormat(mTextureType, mFormat, mUsage, mHwGamma);
	}

	bool TextureProperties::hasAlpha() const
	{
		return PixelUtil::hasAlpha(mFormat);
	}

	UINT32 TextureProperties::getNumFaces() const
	{
		return getTextureType() == TEX_TYPE_CUBE_MAP ? 6 : 1;
	}

	void TextureProperties::mapFromSubresourceIdx(UINT32 subresourceIdx, UINT32& face, UINT32& mip) const
	{
		UINT32 numMipmaps = getNumMipmaps() + 1;

		face = Math::floorToInt((subresourceIdx) / (float)numMipmaps);
		mip = subresourceIdx % numMipmaps;
	}

	UINT32 TextureProperties::mapToSubresourceIdx(UINT32 face, UINT32 mip) const
	{
		return face * (getNumMipmaps() + 1) + mip;
	}

	TextureCore::TextureCore(TextureType textureType, UINT32 width, UINT32 height, UINT32 depth, UINT32 numMipmaps,
		PixelFormat format, int usage, bool hwGamma, UINT32 multisampleCount)
		:mProperties(textureType, width, height, depth, numMipmaps, format, usage, hwGamma, multisampleCount)
	{ }

	void TextureCore::writeSubresource(UINT32 subresourceIdx, const PixelData& pixelData, bool discardEntireBuffer)
	{
		THROW_IF_NOT_CORE_THREAD;

		if(discardEntireBuffer)
		{
			if(mProperties.getUsage() != TU_DYNAMIC)
			{
				LOGWRN("Buffer discard is enabled but buffer was not created as dynamic. Disabling discard.");
				discardEntireBuffer = false;
			}
		}
		else
		{
			if (mProperties.getUsage() == TU_DYNAMIC)
			{
				LOGWRN("Buffer discard is not enabled but buffer was not created as dynamic. Enabling discard.");
				discardEntireBuffer = true;
			}
		}

		UINT32 face = 0;
		UINT32 mip = 0;
		mProperties.mapFromSubresourceIdx(subresourceIdx, face, mip);

		writeData(pixelData, mip, face, discardEntireBuffer);
	}

	void TextureCore::readSubresource(UINT32 subresourceIdx, PixelData& data)
	{
		THROW_IF_NOT_CORE_THREAD;

		UINT32 face = 0;
		UINT32 mip = 0;
		mProperties.mapFromSubresourceIdx(subresourceIdx, face, mip);

		PixelData& pixelData = static_cast<PixelData&>(data);

		UINT32 mipWidth, mipHeight, mipDepth;
		PixelUtil::getSizeForMipLevel(mProperties.getWidth(), mProperties.getHeight(), mProperties.getDepth(),
			mip, mipWidth, mipHeight, mipDepth);

		if (pixelData.getWidth() != mipWidth || pixelData.getHeight() != mipHeight ||
			pixelData.getDepth() != mipDepth || pixelData.getFormat() != mProperties.getFormat())
		{
			BS_EXCEPT(RenderingAPIException, "Provided buffer is not of valid dimensions or format in order to read from this texture.");
		}

		readData(pixelData, mip, face);
	}

	PixelData TextureCore::lock(GpuLockOptions options, UINT32 mipLevel, UINT32 face)
	{
		THROW_IF_NOT_CORE_THREAD;

		if (mipLevel < 0 || mipLevel > mProperties.getNumMipmaps())
			BS_EXCEPT(InvalidParametersException, "Invalid mip level: " + toString(mipLevel) + ". Min is 0, max is " + toString(mProperties.getNumMipmaps()));

		if (face < 0 || face >= mProperties.getNumFaces())
			BS_EXCEPT(InvalidParametersException, "Invalid face index: " + toString(face) + ". Min is 0, max is " + toString(mProperties.getNumFaces()));

		return lockImpl(options, mipLevel, face);
	}

	void TextureCore::unlock()
	{
		THROW_IF_NOT_CORE_THREAD;

		unlockImpl();
	}

	void TextureCore::copy(UINT32 srcSubresourceIdx, UINT32 destSubresourceIdx, const SPtr<TextureCore>& target)
	{
		THROW_IF_NOT_CORE_THREAD;

		if (target->mProperties.getTextureType() != mProperties.getTextureType())
			BS_EXCEPT(InvalidParametersException, "Source and destination textures must be of same type and must have the same usage and type.");

		if (mProperties.getFormat() != target->mProperties.getFormat()) // Note: It might be okay to use different formats of the same size
			BS_EXCEPT(InvalidParametersException, "Source and destination texture formats must match.");

		if (target->mProperties.getMultisampleCount() > 0 && mProperties.getMultisampleCount() != target->mProperties.getMultisampleCount())
			BS_EXCEPT(InvalidParametersException, "When copying to a multisampled texture, source texture must have the same number of samples.");

		UINT32 srcFace = 0;
		UINT32 srcMipLevel = 0;

		UINT32 destFace = 0;
		UINT32 destMipLevel = 0;

		mProperties.mapFromSubresourceIdx(srcSubresourceIdx, srcFace, srcMipLevel);
		target->mProperties.mapFromSubresourceIdx(destSubresourceIdx, destFace, destMipLevel);

		if (destFace >= mProperties.getNumFaces())
			BS_EXCEPT(InvalidParametersException, "Invalid destination face index");

		if (srcFace >= target->mProperties.getNumFaces())
			BS_EXCEPT(InvalidParametersException, "Invalid destination face index");

		if (srcMipLevel > mProperties.getNumMipmaps())
			BS_EXCEPT(InvalidParametersException, "Source mip level out of range. Valid range is [0, " + toString(mProperties.getNumMipmaps()) + "]");

		if (destMipLevel > target->mProperties.getNumMipmaps())
			BS_EXCEPT(InvalidParametersException, "Destination mip level out of range. Valid range is [0, " + toString(target->mProperties.getNumMipmaps()) + "]");

		UINT32 srcMipWidth = mProperties.getWidth() >> srcMipLevel;
		UINT32 srcMipHeight = mProperties.getHeight() >> srcMipLevel;
		UINT32 srcMipDepth = mProperties.getDepth() >> srcMipLevel;

		UINT32 dstMipWidth = target->mProperties.getWidth() >> destMipLevel;
		UINT32 dstMipHeight = target->mProperties.getHeight() >> destMipLevel;
		UINT32 dstMipDepth = target->mProperties.getDepth() >> destMipLevel;

		if (srcMipWidth != dstMipWidth || srcMipHeight != dstMipHeight || srcMipDepth != dstMipDepth)
			BS_EXCEPT(InvalidParametersException, "Source and destination sizes must match");

		copyImpl(srcFace, srcMipLevel, destFace, destMipLevel, target);
	}

	/************************************************************************/
	/* 								TEXTURE VIEW                      		*/
	/************************************************************************/

	TextureViewPtr TextureCore::createView()
	{
		TextureViewPtr viewPtr = bs_core_ptr<TextureView, PoolAlloc>(new (bs_alloc<TextureView, PoolAlloc>()) TextureView());
		viewPtr->_setThisPtr(viewPtr);

		return viewPtr;
	}

	void TextureCore::clearBufferViews()
	{
		mTextureViews.clear();
	}

	TextureViewPtr TextureCore::requestView(const SPtr<TextureCore>& texture, UINT32 mostDetailMip, UINT32 numMips, UINT32 firstArraySlice, UINT32 numArraySlices, GpuViewUsage usage)
	{
		THROW_IF_NOT_CORE_THREAD;

		assert(texture != nullptr);

		TEXTURE_VIEW_DESC key;
		key.mostDetailMip = mostDetailMip;
		key.numMips = numMips;
		key.firstArraySlice = firstArraySlice;
		key.numArraySlices = numArraySlices;
		key.usage = usage;

		auto iterFind = texture->mTextureViews.find(key);
		if (iterFind == texture->mTextureViews.end())
		{
			TextureViewPtr newView = texture->createView();
			newView->initialize(texture, key);
			texture->mTextureViews[key] = new (bs_alloc<TextureViewReference, PoolAlloc>()) TextureViewReference(newView);

			iterFind = texture->mTextureViews.find(key);
		}

		iterFind->second->refCount++;
		return iterFind->second->view;
	}

	void TextureCore::releaseView(const TextureViewPtr& view)
	{
		THROW_IF_NOT_CORE_THREAD;

		assert(view != nullptr);

		SPtr<TextureCore> texture = view->getTexture();

		auto iterFind = texture->mTextureViews.find(view->getDesc());
		if (iterFind == texture->mTextureViews.end())
		{
			BS_EXCEPT(InternalErrorException, "Trying to release a texture view that doesn't exist!");
		}

		iterFind->second->refCount--;

		if (iterFind->second->refCount == 0)
		{
			TextureViewReference* toRemove = iterFind->second;

			texture->mTextureViews.erase(iterFind);

			bs_delete<PoolAlloc>(toRemove);
		}
	}

	Texture::Texture()
	{

	}

	Texture::Texture(TextureType textureType, UINT32 width, UINT32 height, UINT32 depth, UINT32 numMipmaps,
		PixelFormat format, int usage, bool hwGamma, UINT32 multisampleCount)
		:mProperties(textureType, width, height, depth, numMipmaps, format, usage, hwGamma, multisampleCount)
    {
        
    }

	void Texture::initialize()
	{
		mSize = calculateSize();

		// Allocate CPU buffers if needed
		if ((mProperties.getUsage() & TU_CPUCACHED) != 0)
			createCPUBuffers();

		Resource::initialize();
	}

	SPtr<CoreObjectCore> Texture::createCore() const
	{
		const TextureProperties& props = getProperties();

		return TextureCoreManager::instance().createTextureInternal(props.getTextureType(), props.getWidth(), props.getHeight(),
			props.getDepth(), props.getNumMipmaps(), props.getFormat(), props.getUsage(), props.isHardwareGammaEnabled(), props.getMultisampleCount());
	}

	AsyncOp Texture::writeSubresource(CoreAccessor& accessor, UINT32 subresourceIdx, const PixelDataPtr& data, bool discardEntireBuffer)
	{
		updateCPUBuffers(subresourceIdx, *data);

		data->_lock();

		std::function<void(const SPtr<TextureCore>&, UINT32, const PixelDataPtr&, bool, AsyncOp&)> func =
			[&](const SPtr<TextureCore>& texture, UINT32 _subresourceIdx, const PixelDataPtr& _pixData, bool _discardEntireBuffer, AsyncOp& asyncOp)
		{
			texture->writeSubresource(_subresourceIdx, *_pixData, _discardEntireBuffer);
			_pixData->_unlock();
			asyncOp._completeOperation();

		};

		return accessor.queueReturnCommand(std::bind(func, getCore(), subresourceIdx,
			data, discardEntireBuffer, std::placeholders::_1));
	}

	AsyncOp Texture::readSubresource(CoreAccessor& accessor, UINT32 subresourceIdx, const PixelDataPtr& data)
	{
		data->_lock();

		std::function<void(const SPtr<TextureCore>&, UINT32, const PixelDataPtr&, AsyncOp&)> func =
			[&](const SPtr<TextureCore>& texture, UINT32 _subresourceIdx, const PixelDataPtr& _pixData, AsyncOp& asyncOp)
		{
			texture->readSubresource(_subresourceIdx, *_pixData);
			_pixData->_unlock();
			asyncOp._completeOperation();

		};

		return accessor.queueReturnCommand(std::bind(func, getCore(), subresourceIdx,
			data, std::placeholders::_1));
	}

	UINT32 Texture::calculateSize() const
	{
		return mProperties.getNumFaces() * PixelUtil::getMemorySize(mProperties.getWidth(),
			mProperties.getHeight(), mProperties.getDepth(), mProperties.getFormat());
	}

	void Texture::updateCPUBuffers(UINT32 subresourceIdx, const PixelData& pixelData)
	{
		if ((mProperties.getUsage() & TU_CPUCACHED) == 0)
			return;

		if (subresourceIdx >= (UINT32)mCPUSubresourceData.size())
		{
			LOGERR("Invalid subresource index: " + toString(subresourceIdx) + ". Supported range: 0 .. " + toString(mCPUSubresourceData.size()));
			return;
		}

		UINT32 mipLevel;
		UINT32 face;
		mProperties.mapFromSubresourceIdx(subresourceIdx, face, mipLevel);

		UINT32 mipWidth, mipHeight, mipDepth;
		PixelUtil::getSizeForMipLevel(mProperties.getWidth(), mProperties.getHeight(), mProperties.getDepth(),
			mipLevel, mipWidth, mipHeight, mipDepth);

		if (pixelData.getWidth() != mipWidth || pixelData.getHeight() != mipHeight ||
			pixelData.getDepth() != mipDepth || pixelData.getFormat() != mProperties.getFormat())
		{
			LOGERR("Provided buffer is not of valid dimensions or format in order to read from this texture.");
			return;
		}

		if (mCPUSubresourceData[subresourceIdx]->getSize() != pixelData.getSize())
			BS_EXCEPT(InternalErrorException, "Buffer sizes don't match.");

		UINT8* dest = mCPUSubresourceData[subresourceIdx]->getData();
		UINT8* src = pixelData.getData();

		memcpy(dest, src, pixelData.getSize());
	}

	PixelDataPtr Texture::allocateSubresourceBuffer(UINT32 subresourceIdx) const
	{
		UINT32 face = 0;
		UINT32 mip = 0;
		mProperties.mapFromSubresourceIdx(subresourceIdx, face, mip);

		UINT32 numMips = mProperties.getNumMipmaps();
		UINT32 width = mProperties.getWidth();
		UINT32 height = mProperties.getHeight();
		UINT32 depth = mProperties.getDepth();

		UINT32 totalSize = PixelUtil::getMemorySize(width, height, depth, mProperties.getFormat());

		for(UINT32 j = 0; j < mip; j++)
		{
			totalSize = PixelUtil::getMemorySize(width, height, depth, mProperties.getFormat());

			if(width != 1) width /= 2;
			if(height != 1) height /= 2;
			if(depth != 1) depth /= 2;
		}

		PixelDataPtr dst = bs_shared_ptr<PixelData, PoolAlloc>(width, height, depth, mProperties.getFormat());

		dst->allocateInternalBuffer();

		return dst;
	}



	void Texture::readData(PixelData& dest, UINT32 mipLevel, UINT32 face)
	{
		if ((mProperties.getUsage() & TU_CPUCACHED) == 0)
		{
			LOGERR("Attempting to read CPU data from a texture that is created without CPU caching.");
			return;
		}

		UINT32 mipWidth, mipHeight, mipDepth;
		PixelUtil::getSizeForMipLevel(mProperties.getWidth(), mProperties.getHeight(), mProperties.getDepth(),
			mipLevel, mipWidth, mipHeight, mipDepth);

		if (dest.getWidth() != mipWidth || dest.getHeight() != mipHeight ||
			dest.getDepth() != mipDepth || dest.getFormat() != mProperties.getFormat())
		{
			LOGERR("Provided buffer is not of valid dimensions or format in order to read from this texture.");
			return;
		}

		UINT32 subresourceIdx = mProperties.mapToSubresourceIdx(face, mipLevel);
		if (subresourceIdx >= (UINT32)mCPUSubresourceData.size())
		{
			LOGERR("Invalid subresource index: " + toString(subresourceIdx) + ". Supported range: 0 .. " + toString(mCPUSubresourceData.size()));
			return;
		}

		if (mCPUSubresourceData[subresourceIdx]->getSize() != dest.getSize())
			BS_EXCEPT(InternalErrorException, "Buffer sizes don't match.");

		UINT8* srcPtr = mCPUSubresourceData[subresourceIdx]->getData();
		UINT8* destPtr = dest.getData();

		memcpy(destPtr, srcPtr, dest.getSize());
	}

	void Texture::createCPUBuffers()
	{
		UINT32 numFaces = mProperties.getNumFaces();
		UINT32 numMips = mProperties.getNumMipmaps();

		UINT32 numSubresources = numFaces * numMips;
		mCPUSubresourceData.resize(numSubresources);

		for (UINT32 i = 0; i < numFaces; i++)
		{
			UINT32 curWidth = mProperties.getWidth();
			UINT32 curHeight = mProperties.getHeight();
			UINT32 curDepth = mProperties.getDepth();

			for (UINT32 j = 0; j < mProperties.getNumMipmaps(); j++)
			{
				UINT32 subresourceIdx = mProperties.mapToSubresourceIdx(i, j);

				mCPUSubresourceData[subresourceIdx] = bs_shared_ptr<PixelData>(curWidth, curHeight, curDepth, mProperties.getFormat());
				mCPUSubresourceData[subresourceIdx]->allocateInternalBuffer();

				if (curWidth > 1)
					curWidth = curWidth / 2;

				if (curHeight > 1)
					curHeight = curHeight / 2;

				if (curDepth > 1)
					curDepth = curDepth / 2;
			}
		}
	}

	SPtr<TextureCore> Texture::getCore() const
	{
		return std::static_pointer_cast<TextureCore>(mCoreSpecific);
	}

	/************************************************************************/
	/* 								SERIALIZATION                      		*/
	/************************************************************************/

	RTTITypeBase* Texture::getRTTIStatic()
	{
		return TextureRTTI::instance();
	}

	RTTITypeBase* Texture::getRTTI() const
	{
		return Texture::getRTTIStatic();
	}

	/************************************************************************/
	/* 								STATICS	                      			*/
	/************************************************************************/
	HTexture Texture::create(TextureType texType, UINT32 width, UINT32 height, UINT32 depth, 
		int num_mips, PixelFormat format, int usage, bool hwGammaCorrection, UINT32 multisampleCount)
	{
		TexturePtr texturePtr = _createPtr(texType, 
			width, height, depth, num_mips, format, usage, hwGammaCorrection, multisampleCount);

		return static_resource_cast<Texture>(gResources()._createResourceHandle(texturePtr));
	}
	
	HTexture Texture::create(TextureType texType, UINT32 width, UINT32 height, 
		int num_mips, PixelFormat format, int usage, bool hwGammaCorrection, UINT32 multisampleCount)
	{
		TexturePtr texturePtr = _createPtr(texType, 
			width, height, num_mips, format, usage, hwGammaCorrection, multisampleCount);

		return static_resource_cast<Texture>(gResources()._createResourceHandle(texturePtr));
	}

	TexturePtr Texture::_createPtr(TextureType texType, UINT32 width, UINT32 height, UINT32 depth, 
		int num_mips, PixelFormat format, int usage, bool hwGammaCorrection, UINT32 multisampleCount)
	{
		return TextureManager::instance().createTexture(texType, 
			width, height, depth, num_mips, format, usage, hwGammaCorrection, multisampleCount);
	}

	TexturePtr Texture::_createPtr(TextureType texType, UINT32 width, UINT32 height, 
		int num_mips, PixelFormat format, int usage, bool hwGammaCorrection, UINT32 multisampleCount)
	{
		return TextureManager::instance().createTexture(texType, 
			width, height, num_mips, format, usage, hwGammaCorrection, multisampleCount);
	}

	const HTexture& Texture::dummy()
	{
		static HTexture dummyTexture;
		if (!dummyTexture)
		{
			dummyTexture = create(TEX_TYPE_2D, 2, 2, 0, PF_R8G8B8A8);

			UINT32 subresourceIdx = dummyTexture->getProperties().mapToSubresourceIdx(0, 0);
			PixelDataPtr data = dummyTexture->allocateSubresourceBuffer(subresourceIdx);

			data->setColorAt(Color::Red, 0, 0);
			data->setColorAt(Color::Red, 0, 1);
			data->setColorAt(Color::Red, 1, 0);
			data->setColorAt(Color::Red, 1, 1);

			dummyTexture->writeSubresource(gCoreAccessor(), subresourceIdx, data, false);
		}

		return dummyTexture;
	}
}