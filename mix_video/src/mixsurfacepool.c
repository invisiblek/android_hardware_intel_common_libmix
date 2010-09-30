/* 
 INTEL CONFIDENTIAL
 Copyright 2009 Intel Corporation All Rights Reserved.
 The source code contained or described herein and all documents related to the source code ("Material") are owned by Intel Corporation or its suppliers or licensors. Title to the Material remains with Intel Corporation or its suppliers and licensors. The Material contains trade secrets and proprietary and confidential information of Intel or its suppliers and licensors. The Material is protected by worldwide copyright and trade secret laws and treaty provisions. No part of the Material may be used, copied, reproduced, modified, published, uploaded, posted, transmitted, distributed, or disclosed in any way without Intel’s prior express written permission.

 No license under any patent, copyright, trade secret or other intellectual property right is granted to or conferred upon you by disclosure or delivery of the Materials, either expressly, by implication, inducement, estoppel or otherwise. Any license under such intellectual property rights must be express and approved by Intel in writing.
 */

/**
 * SECTION:mixsurfacepool
 * @short_description: MI-X Video Surface Pool
 *
 * A data object which stores and manipulates a pool of video surfaces.
 */

#include "mixvideolog.h"
#include "mixsurfacepool.h"
#include "mixvideoframe_private.h"

#define MIX_LOCK(lock) g_mutex_lock(lock);
#define MIX_UNLOCK(lock) g_mutex_unlock(lock);

#define SAFE_FREE(p) if(p) { g_free(p); p = NULL; }

static GType _mix_surfacepool_type = 0;
static MixParamsClass *parent_class = NULL;

#define _do_init { _mix_surfacepool_type = g_define_type_id; }

gboolean mix_surfacepool_copy(MixParams * target, const MixParams * src);
MixParams *mix_surfacepool_dup(const MixParams * obj);
gboolean mix_surfacepool_equal(MixParams * first, MixParams * second);
static void mix_surfacepool_finalize(MixParams * obj);

G_DEFINE_TYPE_WITH_CODE (MixSurfacePool, mix_surfacepool, MIX_TYPE_PARAMS,
		_do_init);

static void mix_surfacepool_init(MixSurfacePool * self) {
	/* initialize properties here */
	self->free_list = NULL;
	self->in_use_list = NULL;
	self->free_list_max_size = 0;
	self->free_list_cur_size = 0;
	self->high_water_mark = 0;
	self->initialized = FALSE;

	self->reserved1 = NULL;
	self->reserved2 = NULL;
	self->reserved3 = NULL;
	self->reserved4 = NULL;

	// TODO: relocate this mutex allocation -we can't communicate failure in ctor.
	// Note that g_thread_init() has already been called by mix_video_init()
	self->objectlock = g_mutex_new();

}

static void mix_surfacepool_class_init(MixSurfacePoolClass * klass) {
	MixParamsClass *mixparams_class = MIX_PARAMS_CLASS(klass);

	/* setup static parent class */
	parent_class = (MixParamsClass *) g_type_class_peek_parent(klass);

	mixparams_class->finalize = mix_surfacepool_finalize;
	mixparams_class->copy = (MixParamsCopyFunction) mix_surfacepool_copy;
	mixparams_class->dup = (MixParamsDupFunction) mix_surfacepool_dup;
	mixparams_class->equal = (MixParamsEqualFunction) mix_surfacepool_equal;
}

MixSurfacePool *
mix_surfacepool_new(void) {
	MixSurfacePool *ret = (MixSurfacePool *) g_type_create_instance(
			MIX_TYPE_SURFACEPOOL);
	return ret;
}

void mix_surfacepool_finalize(MixParams * obj) {
	/* clean up here. */

	MixSurfacePool *self = MIX_SURFACEPOOL(obj);

	if (self->objectlock) {
		g_mutex_free(self->objectlock);
		self->objectlock = NULL;
	}

	/* Chain up parent */
	if (parent_class->finalize) {
		parent_class->finalize(obj);
	}
}

MixSurfacePool *
mix_surfacepool_ref(MixSurfacePool * mix) {
	return (MixSurfacePool *) mix_params_ref(MIX_PARAMS(mix));
}

/**
 * mix_surfacepool_dup:
 * @obj: a #MixSurfacePool object
 * @returns: a newly allocated duplicate of the object.
 *
 * Copy duplicate of the object.
 */
MixParams *
mix_surfacepool_dup(const MixParams * obj) {
	MixParams *ret = NULL;

	if (MIX_IS_SURFACEPOOL(obj)) {

		MIX_LOCK(MIX_SURFACEPOOL(obj)->objectlock);

		MixSurfacePool *duplicate = mix_surfacepool_new();
		if (mix_surfacepool_copy(MIX_PARAMS(duplicate), MIX_PARAMS(obj))) {
			ret = MIX_PARAMS(duplicate);
		} else {
			mix_surfacepool_unref(duplicate);
		}

		MIX_UNLOCK(MIX_SURFACEPOOL(obj)->objectlock);

	}
	return ret;
}

/**
 * mix_surfacepool_copy:
 * @target: copy to target
 * @src: copy from src
 * @returns: boolean indicates if copy is successful.
 *
 * Copy instance data from @src to @target.
 */
gboolean mix_surfacepool_copy(MixParams * target, const MixParams * src) {
	MixSurfacePool *this_target, *this_src;

	if (MIX_IS_SURFACEPOOL(target) && MIX_IS_SURFACEPOOL(src)) {

		MIX_LOCK(MIX_SURFACEPOOL(src)->objectlock);
		MIX_LOCK(MIX_SURFACEPOOL(target)->objectlock);

		// Cast the base object to this child object
		this_target = MIX_SURFACEPOOL(target);
		this_src = MIX_SURFACEPOOL(src);

		// Free the existing properties

		// Duplicate string
		this_target->free_list = this_src->free_list;
		this_target->in_use_list = this_src->in_use_list;
		this_target->free_list_max_size = this_src->free_list_max_size;
		this_target->free_list_cur_size = this_src->free_list_cur_size;
		this_target->high_water_mark = this_src->high_water_mark;

		MIX_UNLOCK(MIX_SURFACEPOOL(src)->objectlock);
		MIX_UNLOCK(MIX_SURFACEPOOL(target)->objectlock);

		// Now chainup base class
		if (parent_class->copy) {
			return parent_class->copy(MIX_PARAMS_CAST(target), MIX_PARAMS_CAST(
					src));
		} else {
			return TRUE;
		}
	}
	return FALSE;
}

/**
 * mix_surfacepool_equal:
 * @first: first object to compare
 * @second: seond object to compare
 * @returns: boolean indicates if instance are equal.
 *
 * Copy instance data from @src to @target.
 */
gboolean mix_surfacepool_equal(MixParams * first, MixParams * second) {
	gboolean ret = FALSE;
	MixSurfacePool *this_first, *this_second;

	if (MIX_IS_SURFACEPOOL(first) && MIX_IS_SURFACEPOOL(second)) {
		// Deep compare
		// Cast the base object to this child object

		MIX_LOCK(MIX_SURFACEPOOL(first)->objectlock);
		MIX_LOCK(MIX_SURFACEPOOL(second)->objectlock);

		this_first = MIX_SURFACEPOOL(first);
		this_second = MIX_SURFACEPOOL(second);

		/* TODO: add comparison for other properties */
		if (this_first->free_list == this_second->free_list
				&& this_first->in_use_list == this_second->in_use_list
				&& this_first->free_list_max_size
						== this_second->free_list_max_size
				&& this_first->free_list_cur_size
						== this_second->free_list_cur_size
				&& this_first->high_water_mark == this_second->high_water_mark) {
			// members within this scope equal. chaining up.
			MixParamsClass *klass = MIX_PARAMS_CLASS(parent_class);
			if (klass->equal)
				ret = klass->equal(first, second);
			else
				ret = TRUE;
		}

		MIX_LOCK(MIX_SURFACEPOOL(first)->objectlock);
		MIX_LOCK(MIX_SURFACEPOOL(second)->objectlock);

	}

	return ret;
}

/*  Class Methods  */

/**
 * mix_surfacepool_initialize:
 * @returns: MIX_RESULT_SUCCESS if successful in creating the surface pool
 *
 * Use this method to create a new surface pool, consisting of a GSList of
 * frame objects that represents a pool of surfaces.
 */
MIX_RESULT mix_surfacepool_initialize(MixSurfacePool * obj,
		VASurfaceID *surfaces, guint num_surfaces, VADisplay va_display) {

	LOG_V( "Begin\n");

	if (obj == NULL || surfaces == NULL) {

		LOG_E(
				"Error NULL ptrs, obj %x, surfaces %x\n", (guint) obj,
				(guint) surfaces);

		return MIX_RESULT_NULL_PTR;
	}

	MIX_LOCK(obj->objectlock);

	if ((obj->free_list != NULL) || (obj->in_use_list != NULL)) {
		//surface pool is in use; return error; need proper cleanup
		//TODO need cleanup here?

		MIX_UNLOCK(obj->objectlock);

		return MIX_RESULT_ALREADY_INIT;
	}

	if (num_surfaces == 0) {
		obj->free_list = NULL;

		obj->in_use_list = NULL;

		obj->free_list_max_size = num_surfaces;

		obj->free_list_cur_size = num_surfaces;

		obj->high_water_mark = 0;

        /* assume it is initialized */
        obj->initialized = TRUE;
        
		MIX_UNLOCK(obj->objectlock);

		return MIX_RESULT_SUCCESS;
	}

	// Initialize the free pool with frame objects

	gint i = 0;
	MixVideoFrame *frame = NULL;

	for (; i < num_surfaces; i++) {

		//Create a frame object for each surface ID
		frame = mix_videoframe_new();

		if (frame == NULL) {
			//TODO need to log an error here and do cleanup

			MIX_UNLOCK(obj->objectlock);

			return MIX_RESULT_NO_MEMORY;
		}

		// Set the frame ID to the surface ID
		mix_videoframe_set_frame_id(frame, surfaces[i]);
		// Set the ci frame index to the surface ID		
		mix_videoframe_set_ci_frame_idx (frame, i);			
		// Leave timestamp for each frame object as zero
		// Set the pool reference in the private data of the frame object
		mix_videoframe_set_pool(frame, obj);

		mix_videoframe_set_vadisplay(frame, va_display);

		//Add each frame object to the pool list
		obj->free_list = g_slist_append(obj->free_list, frame);

	}

	obj->in_use_list = NULL;

	obj->free_list_max_size = num_surfaces;

	obj->free_list_cur_size = num_surfaces;

	obj->high_water_mark = 0;

    obj->initialized = TRUE;

	MIX_UNLOCK(obj->objectlock);

	LOG_V( "End\n");

	return MIX_RESULT_SUCCESS;
}

/**
 * mix_surfacepool_put:
 * @returns: SUCCESS or FAILURE
 *
 * Use this method to return a surface to the free pool
 */
MIX_RESULT mix_surfacepool_put(MixSurfacePool * obj, MixVideoFrame * frame) {
	
	LOG_V( "Begin\n");
	if (obj == NULL || frame == NULL)
		return MIX_RESULT_NULL_PTR;

	LOG_V( "Frame id: %d\n", frame->frame_id);
	MIX_LOCK(obj->objectlock);

	if (obj->in_use_list == NULL) {
		//in use list cannot be empty if a frame is in use
		//TODO need better error code for this

		MIX_UNLOCK(obj->objectlock);

		return MIX_RESULT_FAIL;
	}

	GSList *element = g_slist_find(obj->in_use_list, frame);
	if (element == NULL) {
		//Integrity error; frame not found in in use list
		//TODO need better error code and handling for this

		MIX_UNLOCK(obj->objectlock);

		return MIX_RESULT_FAIL;
	} else {
		//Remove this element from the in_use_list
		obj->in_use_list = g_slist_remove_link(obj->in_use_list, element);

		//Concat the element to the free_list and reset the timestamp of the frame
		//Note that the surface ID stays valid
		mix_videoframe_set_timestamp(frame, 0);
		obj->free_list = g_slist_concat(obj->free_list, element);
		
		//increment the free list count
		obj->free_list_cur_size++;
	}

	//Note that we do nothing with the ref count for this.  We want it to
	//stay at 1, which is what triggered it to be added back to the free list.

	MIX_UNLOCK(obj->objectlock);

	LOG_V( "End\n");
	return MIX_RESULT_SUCCESS;
}

/**
 * mix_surfacepool_get:
 * @returns: SUCCESS or FAILURE
 *
 * Use this method to get a surface from the free pool
 */
MIX_RESULT mix_surfacepool_get(MixSurfacePool * obj, MixVideoFrame ** frame) {

	LOG_V( "Begin\n");

	if (obj == NULL || frame == NULL)
		return MIX_RESULT_NULL_PTR;

	MIX_LOCK(obj->objectlock);

#if 0
	if (obj->free_list == NULL) {
#else
	if (obj->free_list_cur_size <= 1) {  //Keep one surface free at all times for VBLANK bug
#endif
		//We are out of surfaces
		//TODO need to log this as well

		MIX_UNLOCK(obj->objectlock);

		LOG_E( "out of surfaces\n");

		return MIX_RESULT_NO_MEMORY;
	}

	//Remove a frame from the free pool

	//We just remove the one at the head, since it's convenient
	GSList *element = obj->free_list;
	obj->free_list = g_slist_remove_link(obj->free_list, element);
	if (element == NULL) {
		//Unexpected behavior
		//TODO need better error code and handling for this

		MIX_UNLOCK(obj->objectlock);

		LOG_E( "Element is null\n");

		return MIX_RESULT_FAIL;
	} else {
		//Concat the element to the in_use_list
		obj->in_use_list = g_slist_concat(obj->in_use_list, element);

		//TODO replace with proper logging

		LOG_I( "frame refcount%d\n",
				MIX_PARAMS(element->data)->refcount);

		//Set the out frame pointer
		*frame = (MixVideoFrame *) element->data;

		LOG_V( "Frame id: %d\n", (*frame)->frame_id);
		
		//decrement the free list count
		obj->free_list_cur_size--;

		//Check the high water mark for surface use
		guint size = g_slist_length(obj->in_use_list);
		if (size > obj->high_water_mark)
			obj->high_water_mark = size;
		//TODO Log this high water mark
	}

	//Increment the reference count for the frame
	mix_videoframe_ref(*frame);

	MIX_UNLOCK(obj->objectlock);

	LOG_V( "End\n");

	return MIX_RESULT_SUCCESS;
}


gint mixframe_compare_index (MixVideoFrame * a, MixVideoFrame * b)
{
    if (a == NULL || b == NULL)
	 return -1;
    if (a->ci_frame_idx == b->ci_frame_idx)
        return 0;
    else 
        return -1;	
}

/**
 * mix_surfacepool_get:
 * @returns: SUCCESS or FAILURE
 *
 * Use this method to get a surface from the free pool according to the CI frame idx
 */

MIX_RESULT mix_surfacepool_get_frame_with_ci_frameidx (MixSurfacePool * obj, MixVideoFrame ** frame, MixVideoFrame *in_frame) {

	LOG_V( "Begin\n");

	if (obj == NULL || frame == NULL)
		return MIX_RESULT_NULL_PTR;

	MIX_LOCK(obj->objectlock);

	if (obj->free_list == NULL) {
		//We are out of surfaces
		//TODO need to log this as well

		MIX_UNLOCK(obj->objectlock);

		LOG_E( "out of surfaces\n");

		return MIX_RESULT_NO_MEMORY;
	}

	//Remove a frame from the free pool

	//We just remove the one at the head, since it's convenient
	GSList *element = g_slist_find_custom (obj->free_list, in_frame, (GCompareFunc) mixframe_compare_index);
	obj->free_list = g_slist_remove_link(obj->free_list, element);
	if (element == NULL) {
		//Unexpected behavior
		//TODO need better error code and handling for this

		MIX_UNLOCK(obj->objectlock);

		LOG_E( "Element is null\n");

		return MIX_RESULT_FAIL;
	} else {
		//Concat the element to the in_use_list
		obj->in_use_list = g_slist_concat(obj->in_use_list, element);

		//TODO replace with proper logging

		LOG_I( "frame refcount%d\n",
				MIX_PARAMS(element->data)->refcount);

		//Set the out frame pointer
		*frame = (MixVideoFrame *) element->data;

		//Check the high water mark for surface use
		guint size = g_slist_length(obj->in_use_list);
		if (size > obj->high_water_mark)
			obj->high_water_mark = size;
		//TODO Log this high water mark
	}

	//Increment the reference count for the frame
	mix_videoframe_ref(*frame);

	MIX_UNLOCK(obj->objectlock);

	LOG_V( "End\n");

	return MIX_RESULT_SUCCESS;
}
/**
 * mix_surfacepool_check_available:
 * @returns: SUCCESS or FAILURE
 *
 * Use this method to check availability of getting a surface from the free pool
 */
MIX_RESULT mix_surfacepool_check_available(MixSurfacePool * obj) {

	LOG_V( "Begin\n");

	if (obj == NULL)
		return MIX_RESULT_NULL_PTR;

	MIX_LOCK(obj->objectlock);

    if (obj->initialized == FALSE)
    {
        LOG_W("surface pool is not initialized, probably configuration data has not been received yet.\n");
        MIX_UNLOCK(obj->objectlock);
        return MIX_RESULT_NOT_INIT;
    }

    
#if 0
	if (obj->free_list == NULL) {
#else
	if (obj->free_list_cur_size <= 1) {  //Keep one surface free at all times for VBLANK bug
#endif
		//We are out of surfaces

		MIX_UNLOCK(obj->objectlock);

		LOG_W(
				"Returning MIX_RESULT_POOLEMPTY because out of surfaces\n");

		return MIX_RESULT_POOLEMPTY;
	} else {
		//Pool is not empty

		MIX_UNLOCK(obj->objectlock);

		LOG_I(
				"Returning MIX_RESULT_SUCCESS because surfaces are available\n");

		return MIX_RESULT_SUCCESS;
	}

}

/**
 * mix_surfacepool_deinitialize:
 * @returns: SUCCESS or FAILURE
 *
 * Use this method to teardown a surface pool
 */
MIX_RESULT mix_surfacepool_deinitialize(MixSurfacePool * obj) {
	if (obj == NULL)
		return MIX_RESULT_NULL_PTR;

	MIX_LOCK(obj->objectlock);

	if ((obj->in_use_list != NULL) || (g_slist_length(obj->free_list)
			!= obj->free_list_max_size)) {
		//TODO better error code
		//We have outstanding frame objects in use and they need to be
		//freed before we can deinitialize.

		MIX_UNLOCK(obj->objectlock);

		return MIX_RESULT_FAIL;
	}

	//Now remove frame objects from the list

	MixVideoFrame *frame = NULL;

	while (obj->free_list != NULL) {
		//Get the frame object from the head of the list
		frame = obj->free_list->data;
		//frame = g_slist_nth_data(obj->free_list, 0);

		//Release it
		mix_videoframe_unref(frame);

		//Delete the head node of the list and store the new head
		obj->free_list = g_slist_delete_link(obj->free_list, obj->free_list);

		//Repeat until empty
	}

	obj->free_list_max_size = 0;
	obj->free_list_cur_size = 0;

	//May want to log this information for tuning
	obj->high_water_mark = 0;

	MIX_UNLOCK(obj->objectlock);

	return MIX_RESULT_SUCCESS;
}

#define MIX_SURFACEPOOL_SETTER_CHECK_INPUT(obj) \
	if(!obj) return MIX_RESULT_NULL_PTR; \
	if(!MIX_IS_SURFACEPOOL(obj)) return MIX_RESULT_FAIL; \

#define MIX_SURFACEPOOL_GETTER_CHECK_INPUT(obj, prop) \
	if(!obj || !prop) return MIX_RESULT_NULL_PTR; \
	if(!MIX_IS_SURFACEPOOL(obj)) return MIX_RESULT_FAIL; \


MIX_RESULT
mix_surfacepool_dumpframe(MixVideoFrame *frame)
{
	LOG_I( "\tFrame %x, id %lu, refcount %d, ts %lu\n", (guint)frame,
			frame->frame_id, MIX_PARAMS(frame)->refcount, (gulong) frame->timestamp);

	return MIX_RESULT_SUCCESS;
}

MIX_RESULT
mix_surfacepool_dumpprint (MixSurfacePool * obj)
{
	//TODO replace this with proper logging later

	LOG_I( "SURFACE POOL DUMP:\n");
	LOG_I( "Free list size is %d\n", obj->free_list_cur_size);
	LOG_I( "In use list size is %d\n", g_slist_length(obj->in_use_list));
	LOG_I( "High water mark is %lu\n", obj->high_water_mark);

	//Walk the free list and report the contents
	LOG_I( "Free list contents:\n");
	g_slist_foreach(obj->free_list, (GFunc) mix_surfacepool_dumpframe, NULL);

	//Walk the in_use list and report the contents
	LOG_I( "In Use list contents:\n");
	g_slist_foreach(obj->in_use_list, (GFunc) mix_surfacepool_dumpframe, NULL);

	return MIX_RESULT_SUCCESS;
}
