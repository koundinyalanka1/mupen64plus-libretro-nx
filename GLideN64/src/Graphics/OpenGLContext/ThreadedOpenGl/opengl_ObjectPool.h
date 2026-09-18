#pragma once

#include <atomic>
#include <memory>
#include <vector>


namespace opengl {

	class PoolObject {
	public:

		PoolObject();

		bool isInUse();
		void setInUse(bool _inUse);

		int getPoolId();

		void setPoolId(int _poolId);

		int getObjectId();

		void setObjectId(int _objectId);
	private:

		/* Cleared by the render thread as it finishes a command, read by the
		 * producer looking for a free object. Previously a plain bool that
		 * relied on the command mutex for visibility. */
		std::atomic<bool> m_inUse;
		int m_poolId;
		int m_objectId;
	};

	class OpenGlCommandPool
	{
	public:
		static OpenGlCommandPool& get();

		int getNextAvailablePool();

		/* Borrowed pointer. The pool's vector owns every object and never
		 * removes one, so these stay valid for the process lifetime;
		 * recycling is governed by setInUse(), not by ownership. */
		PoolObject * getAvailableObject(int _poolId);

		void addObjectToPool(int _poolId, std::shared_ptr<PoolObject> _object);

	private:
		std::vector<std::vector<std::shared_ptr<PoolObject>>> m_objectPool;
		std::vector<unsigned int> m_objectPoolIndex;
	};
}
