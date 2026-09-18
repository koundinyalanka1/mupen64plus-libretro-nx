#pragma once

#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include <string>
#include <condition_variable>
#include "opengl_ObjectPool.h"
#include "RingBufferPool.h"

namespace opengl {

	class OpenGlCommand : public PoolObject {
	public:
		void performCommandSingleThreaded();

		void performCommand();

		void waitOnCommand();
#ifdef GL_DEBUG
		std::string getFunctionName();
#endif

		virtual bool isTimeToShutdown();

		static RingBufferPool m_ringBufferPool;

	protected:
		OpenGlCommand(bool _synced, bool _logIfSynced, const std::string &_functionName,
			bool _isGlCommand = true);

		virtual void commandToExecute() = 0;

		/* Returns a borrowed pointer. Commands live in the pool for the
		 * process lifetime, so refcounting them bought no safety and cost
		 * several atomic round-trips on every GL call. */
		template<typename CoomandType>
		static CoomandType * getFromPool(int _poolId) {
			PoolObject * poolObject = OpenGlCommandPool::get().getAvailableObject(_poolId);
			if (poolObject == nullptr) {
				std::shared_ptr<CoomandType> newObject(new CoomandType);
				OpenGlCommandPool::get().addObjectToPool(_poolId, newObject);
				poolObject = newObject.get();
			}

			poolObject->setInUse(true);
			return static_cast<CoomandType *>(poolObject);
		}

#ifdef GL_DEBUG
		const bool m_logIfSynced;
		const std::string m_functionName;
		const bool m_isGlCommand;
#endif

	private:
		/* Set once by the constructor and never written again. */
		const bool m_synced;
		bool m_executed;
		std::mutex m_condvarMutex;
		std::condition_variable m_condition;
	};
}
