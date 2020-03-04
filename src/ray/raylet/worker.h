#ifndef RAY_RAYLET_WORKER_H
#define RAY_RAYLET_WORKER_H

#include <memory>

#include "ray/common/client_connection.h"
#include "ray/common/id.h"
#include "ray/common/task/scheduling_resources.h"
#include "ray/common/task/task.h"
#include "ray/common/task/task_common.h"
#include "ray/rpc/worker/core_worker_client.h"
#include "ray/util/process.h"
#include "ray/common/scheduling/scheduling_ids.h"
#include "ray/common/scheduling/cluster_resource_scheduler.h"

namespace ray {

namespace raylet {

/// Worker class encapsulates the implementation details of a worker. A worker
/// is the execution container around a unit of Ray work, such as a task or an
/// actor. Ray units of work execute in the context of a Worker.
class Worker {
 public:
  /// A constructor that initializes a worker object.
  /// NOTE: You MUST manually set the worker process.
  Worker(const WorkerID &worker_id, const Language &language, int port,
         std::shared_ptr<LocalClientConnection> connection,
         rpc::ClientCallManager &client_call_manager);
  /// A destructor responsible for freeing all worker state.
  ~Worker() {}
  void MarkDead();
  bool IsDead() const;
  void MarkBlocked();
  void MarkUnblocked();
  bool IsBlocked() const;
  /// Return the worker's ID.
  WorkerID WorkerId() const;
  /// Return the worker process.
  Process GetProcess() const;
  void SetProcess(Process proc);
  Language GetLanguage() const;
  int Port() const;
  void AssignTaskId(const TaskID &task_id);
  const TaskID &GetAssignedTaskId() const;
  bool AddBlockedTaskId(const TaskID &task_id);
  bool RemoveBlockedTaskId(const TaskID &task_id);
  const std::unordered_set<TaskID> &GetBlockedTaskIds() const;
  void AssignJobId(const JobID &job_id);
  const JobID &GetAssignedJobId() const;
  void AssignActorId(const ActorID &actor_id);
  const ActorID &GetActorId() const;
  void MarkDetachedActor();
  bool IsDetachedActor() const;
  const std::shared_ptr<LocalClientConnection> Connection() const;
  void SetOwnerAddress(const rpc::Address &address);
  const rpc::Address &GetOwnerAddress() const;

  const ResourceIdSet &GetLifetimeResourceIds() const;
  void SetLifetimeResourceIds(ResourceIdSet &resource_ids);
  void ResetLifetimeResourceIds();

  const ResourceIdSet &GetTaskResourceIds() const;
  void SetTaskResourceIds(ResourceIdSet &resource_ids);
  void ResetTaskResourceIds();
  ResourceIdSet ReleaseTaskCpuResources();
  void AcquireTaskCpuResources(const ResourceIdSet &cpu_resources);

  const std::unordered_set<ObjectID> &GetActiveObjectIds() const;
  void SetActiveObjectIds(const std::unordered_set<ObjectID> &&object_ids);

  Status AssignTask(const Task &task, const ResourceIdSet &resource_id_set);
  void DirectActorCallArgWaitComplete(int64_t tag);
  void WorkerLeaseGranted(const std::string &address, int port);

  /// Cpus borrowed by the worker. This happens when the machine is oversubscribed
  /// and the worker does not get back the cpu resources when unblocked.
  /// TODO (ion): Add methods to access this variable.
  /// TODO (ion): Investigate a more intuitive alternative to track these Cpus.
  /// XXX
  TaskResourceInstances allocated_instances_;
  void SetAllocatedInstances(TaskResourceInstances &allocated_instances) { 
      allocated_instances_ = allocated_instances;
  };                                                   
  TaskResourceInstances &GetAllocatedInstances() {return allocated_instances_; };    
  void ClearAllocatedInstances() {
    TaskResourceInstances nothing;  
    allocated_instances_ = nothing; // Clear allocated instances.
  };    
  TaskResourceInstances lifetime_allocated_instances_;
  void SetLifetimeAllocatedInstances(TaskResourceInstances &allocated_instances) { 
      lifetime_allocated_instances_ = allocated_instances;
  };                                                   
  TaskResourceInstances &GetLifetimeAllocatedInstances() {return lifetime_allocated_instances_; };    
  void ClearLifetimeAllocatedInstances() {
    TaskResourceInstances nothing;  
    lifetime_allocated_instances_ = nothing; // Clear allocated instances.
  };    
  std::vector<double> borrowed_cpu_instances_;
  void SetBorrowedCPUInstances(std::vector<double> &cpu_instances) { 
    borrowed_cpu_instances_ = cpu_instances; 
  };
  std::vector<double> &GetBorrowedCPUInstances() { return borrowed_cpu_instances_; }; 
  void ClearBorrowedCPUInstances() { return borrowed_cpu_instances_.clear(); };   

  Task assigned_task_;
  Task &GetAssignedTask() { return assigned_task_; };
  void SetAssignedTask(Task &assigned_task) { assigned_task_ = assigned_task; };
  /// XXX                                               

  rpc::CoreWorkerClient *rpc_client() { return rpc_client_.get(); }

 private:
  /// The worker's ID.
  WorkerID worker_id_;
  /// The worker's process.
  Process proc_;
  /// The language type of this worker.
  Language language_;
  /// Port that this worker listens on.
  /// If port <= 0, this indicates that the worker will not listen to a port.
  int port_;
  /// Connection state of a worker.
  std::shared_ptr<LocalClientConnection> connection_;
  /// The worker's currently assigned task.
  TaskID assigned_task_id_;
  /// Job ID for the worker's current assigned task.
  JobID assigned_job_id_;
  /// The worker's actor ID. If this is nil, then the worker is not an actor.
  ActorID actor_id_;
  /// Whether the worker is dead.
  bool dead_;
  /// Whether the worker is blocked. Workers become blocked in a `ray.get`, if
  /// they require a data dependency while executing a task.
  bool blocked_;
  /// The specific resource IDs that this worker owns for its lifetime. This is
  /// only used for actors.
  ResourceIdSet lifetime_resource_ids_;
  /// The specific resource IDs that this worker currently owns for the duration
  // of a task.
  ResourceIdSet task_resource_ids_;
  std::unordered_set<TaskID> blocked_task_ids_;
  /// The `ClientCallManager` object that is shared by `CoreWorkerClient` from all
  /// workers.
  rpc::ClientCallManager &client_call_manager_;
  /// The rpc client to send tasks to this worker.
  std::unique_ptr<rpc::CoreWorkerClient> rpc_client_;
  /// Whether the worker is detached. This is applies when the worker is actor.
  /// Detached actor means the actor's creator can exit without killing this actor.
  bool is_detached_actor_;
  /// The address of this worker's owner. The owner is the worker that
  /// currently holds the lease on this worker, if any.
  rpc::Address owner_address_;
};

}  // namespace raylet

}  // namespace ray

#endif  // RAY_RAYLET_WORKER_H
