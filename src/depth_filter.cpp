// This file is part of SVO - Semi-direct Visual Odometry.
//
// Copyright (C) 2014 Christian Forster <forster at ifi dot uzh dot ch>
// (Robotics and Perception Group, University of Zurich, Switzerland).
//
// SVO is free software: you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or any later version.
//
// SVO is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <algorithm>
#include <vikit/math_utils.h>
#include <vikit/abstract_camera.h>
#include <vikit/vision.h>
#include <boost/bind.hpp>
#include <boost/math/distributions/normal.hpp>
#include <svo/global.h>
#include <svo/depth_filter.h>
#include <svo/frame.h>
#include <svo/point.h>
#include <svo/feature.h>
#include <svo/matcher.h>
#include <svo/config.h>
#include <svo/feature_detection.h>

namespace svo {

	//�ṹ�徲̬����
	int Seed::batch_counter = 0;
	int Seed::seed_counter = 0;

	Seed::Seed(Feature* ftr, float depth_mean, float depth_min) :
		batch_id(batch_counter),		//������Seed�Ĺؼ�֡Id
		id(seed_counter++),				//���������ӻ�
		ftr(ftr),						//�����㣨��Ҫ������ȵ������㣩
		a(10),							//Beta�ֲ��е�a����
		b(10),							//Beta�ֲ��е�b����
		mu(1.0 / depth_mean),			//��̬�ֲ��ĳ�ʼ��ֵ������Ϊƽ����ȵĵ��������
		z_range(1.0 / depth_min),		//�������
		sigma2(z_range*z_range / 36)	//Patch covariance in reference image.
	{}

	//����˲����ĳ�ʼ��
	//���룺	feature				feature_detector
	//			seed_converged_cb	һ������ָ�룬�ڴ�֮ǰ��FrameHandlerMono::initialize()�����Ѿ���seed_converged_cb�󶨵�
	//								��ͼ�к�ѡ�ؼ���ĳ�Ա����newCandidatePoint()
	DepthFilter::DepthFilter(feature_detection::DetectorPtr feature_detector, callback_t seed_converged_cb) :
		feature_detector_(feature_detector),
		seed_converged_cb_(seed_converged_cb),
		seeds_updating_halt_(false),
		thread_(NULL),
		new_keyframe_set_(false),
		new_keyframe_min_depth_(0.0),
		new_keyframe_mean_depth_(0.0)
	{}

	DepthFilter::~DepthFilter()
	{
		stopThread();
		SVO_INFO_STREAM("DepthFilter destructed.");
	}

	void DepthFilter::startThread()
	{
		//��������˲������Ӹ����̣߳�
		thread_ = new boost::thread(&DepthFilter::updateSeedsLoop, this);
	}

	void DepthFilter::stopThread()
	{
		SVO_INFO_STREAM("DepthFilter stop thread invoked.");
		if (thread_ != NULL)
		{
			SVO_INFO_STREAM("DepthFilter interrupt and join thread... ");
			seeds_updating_halt_ = true;
			thread_->interrupt();
			thread_->join();
			thread_ = NULL;
		}
	}

	//��ͨ֡����˲�������ʵ������һ������£�����˲��߳���һֱ���ŵģ�Ҳ��thread_һ�㶼�ǲ�����NULL�ģ�
	//��ˣ�һ�㶼��ִ��if�е���䣬�������updateSeeds()��ʵ���ϣ��ں�̨��������һ������˲��̣߳����ܴ�������״̬�����̲߳�ΪNULL����
	//һֱ�ڵȴ�֡��������֡���룬һ����֡���룬notify_one()�ͻỽ������˲��̣߳�������˲��߳��а�ִ����updateSeedsLoop()������
	//�ú���ͬ�������updateSeeds()����������������˲������Կ�������������»��ǽ���ר�ŵ�����˲��߳�����������˲��������������߳�
	//����ɣ���
	void DepthFilter::addFrame(FramePtr frame)
	{
		if (thread_ != NULL)
		{
			{
				lock_t lock(frame_queue_mut_);
				//����SVOΪ���ٶȣ�����Ƿ�����̫�࣬֡�����в�����2֡��
				//������֡�ͻᶪ����һ֡������
				if (frame_queue_.size() > 2)
					frame_queue_.pop();
				frame_queue_.push(frame);
			}
			seeds_updating_halt_ = false;
			frame_queue_cond_.notify_one();
		}
		else
			updateSeeds(frame);
	}

	// Add new keyframe to the queue
	//���ڹؼ�֡������Ҫ��ӽ�֡���У�ֻ�ǽ�����Ϊ�ؼ�֡�Ϳ����ˣ�Ȼ���̨����˲��߳�ͬ���ᱻ���ѣ�ȥ�������µĹؼ�֡��
	//��addFrame()һ��������addKeyframe()�����������̣߳����һ��Ҳ����ִ��else�е���䣬������ר�ŵ�����˲��߳������������˲�
	void DepthFilter::addKeyframe(FramePtr frame, double depth_mean, double depth_min)
	{
		new_keyframe_min_depth_ = depth_min;
		new_keyframe_mean_depth_ = depth_mean;
		if (thread_ != NULL)
		{
			new_keyframe_ = frame;
			new_keyframe_set_ = true;
			seeds_updating_halt_ = true;
			frame_queue_cond_.notify_one();
		}
		else
			initializeSeeds(frame);
	}

	//�ؼ�֡��������ʼ��������
	void DepthFilter::initializeSeeds(FramePtr frame)
	{
		Features new_features;
		feature_detector_->setExistingFeatures(frame->fts_);
		feature_detector_->detect(frame.get(), frame->img_pyr_,
			Config::triangMinCornerScore(), new_features);

		// initialize a seed for every new feature
		seeds_updating_halt_ = true;
		lock_t lock(seeds_mut_); // by locking the updateSeeds function stops
		++Seed::batch_counter;
		std::for_each(new_features.begin(), new_features.end(), [&](Feature* ftr) {
			seeds_.push_back(Seed(ftr, new_keyframe_mean_depth_, new_keyframe_min_depth_));
		});

		if (options_.verbose)
			SVO_INFO_STREAM("DepthFilter: Initialized " << new_features.size() << " new seeds");
		seeds_updating_halt_ = false;
	}

	void DepthFilter::removeKeyframe(FramePtr frame)
	{
		seeds_updating_halt_ = true;
		lock_t lock(seeds_mut_);
		list<Seed>::iterator it = seeds_.begin();
		size_t n_removed = 0;
		while (it != seeds_.end())
		{
			if (it->ftr->frame == frame.get())
			{
				it = seeds_.erase(it);
				++n_removed;
			}
			else
				++it;
		}
		seeds_updating_halt_ = false;
	}

	void DepthFilter::reset()
	{
		seeds_updating_halt_ = true;
		{
			lock_t lock(seeds_mut_);
			seeds_.clear();
		}
		lock_t lock();
		while (!frame_queue_.empty())
			frame_queue_.pop();
		seeds_updating_halt_ = false;

		if (options_.verbose)
			SVO_INFO_STREAM("DepthFilter: RESET.");
	}

	//A thread that is continuously updating the seeds.
	//����˲��߳���������˲�
	void DepthFilter::updateSeedsLoop()
	{
		while (!boost::this_thread::interruption_requested())
		{
			FramePtr frame;
			{
				lock_t lock(frame_queue_mut_);
				while (frame_queue_.empty() && new_keyframe_set_ == false)
					frame_queue_cond_.wait(lock);
				//������¹ؼ�֡���룬�����֡����
				if (new_keyframe_set_)
				{
					//����new_keyframe_set_Ϊfalse
					new_keyframe_set_ = false;
					//��Ȼ����ֹͣ���������ˣ����øñ��Ϊfalse
					seeds_updating_halt_ = false;
					clearFrameQueue();
					frame = new_keyframe_;
				}
				else
				{
					frame = frame_queue_.front();
					frame_queue_.pop();
				}
			}
			//���Ӹ���
			updateSeeds(frame);
			if (frame->isKeyframe())
				initializeSeeds(frame);
		}
	}

	//���������ӽ��и��£����������Լ���ȷ���ȣ�
	void DepthFilter::updateSeeds(FramePtr frame)
	{
		// update only a limited number of seeds, because we don't have time to do it
		// for all the seeds in every frame!
		//Ϊ��֤�ٶȣ�ֻ�ܸ���һ������������
		size_t n_updates = 0, n_failed_matches = 0, n_seeds = seeds_.size();
		lock_t lock(seeds_mut_);
		list<Seed>::iterator it = seeds_.begin();		//��ȡ�����б��еĵ�һ������

		//*************************����������ȷ���ȣ�*************************//
		//�ο���REODE: Probabilistic, Monocular Dense Reconstruction in Real Time
		//*************************����������ȷ���ȣ�*************************//
		const double focal_length = frame->cam_->errorMultiplier2();		//��ȡ����Ľ��ࣨ��������ѡ����������Ͷ��������������������fx
		double px_noise = 1.0;												//���λ�������̶�Ϊ1������
		double px_error_angle = atan(px_noise / (2.0*focal_length))*2.0;	//law of chord (sehnensatz)��ʽ��16��������beta+

		while (it != seeds_.end())
		{
			// set this value true when seeds updating should be interrupted
			//���seeds_updateing_halt_������Ϊtrue����ô�����˲�����ͣ�����¹ؼ�֡�����ʱ�򣬽�����ͣ�����˲����������¿�����
			if (seeds_updating_halt_)
				return;

			// check if seed is not already too old
			//��Щ���ӣ����۽��ж��ٴθ��£����˶���֡����Ҳû���������Ǿͽ�������Ӽ���ɾ�������ڶ�����и��£��ٽ�����ȥҲ���棩
			if ((Seed::batch_counter - it->batch_id) > options_.max_n_kfs) {
				it = seeds_.erase(it);
				continue;
			}

			// check if point is visible in the current image
			//ftr�Ǹ����Ӷ�Ӧ��������frame�Ǵ�����������Ĺؼ�֡��T_f_w_�Ǵ����絽���֡����ϵ��SE3����frame�ǵ�ǰ֡
			//����������ͨ֡Ҳ�����ǹؼ�֡�������ռ���õ��ӵ�ǰ֡���ο�֡��SE3����
			SE3 T_ref_cur = it->ftr->frame->T_f_w_ * frame->T_f_w_.inverse();

			//mu�����ӵ�ƽ�����ע����ƽ�������̬�ֲ��ľ�ֵ��Ҳ��������һ���˲��õ��ĺ����������f�����������ߵķ���������
			//Ҳ���Ǹ����������ߵķ������������ɹ�һ��ͼ�������һ���õ���1.0 / it->mu * it->ftr->f�õ��������3D����
			//��ʹ��ƽ����Ȼָ������߷����������������ܳ��������Բο�֡����ǰ֡��SE3���󣬵õ��õ��ڵ�ǰ֡�е�3D���꣬
			//ע����������洢����������������ڴ��������ӵĹؼ�֡�е�����
			const Vector3d xyz_f(T_ref_cur.inverse()*(1.0 / it->mu * it->ftr->f));

			//��֤�������Ƿ��ڵ�ǰ֡��ǰ�棬������ڣ���Ȼ�ǿ������ģ�����
			if (xyz_f.z() < 0.0) {
				++it; // behind the camera
				continue;
			}
			//���������ͶӰ����ǰ֡���Ƿ����뵱ǰ֡��ͼ�������ڣ�f2c���Ƚ�3D�����һ����Ȼ������ڲξ���õ���������
			if (!frame->cam_->isInFrame(frame->f2c(xyz_f).cast<int>())) {
				++it; // point does not project in image
				continue;
			}

			// we are using inverse depth coordinates
			//sigma2��Patch covariance in reference image.
			//it->mu��������һ�θ��º�ĺ��������ƽ���������Ҫ��ǰ���µ��²�����
			//it->sigma2��������һ�θ��º�ĺ��������ƽ������ķ����ȷ���ȣ�������Ҫ��ǰ���µ��²�����
			//������һ�ι��Ƶĺ���ƽ�������Լ�����Ĳ�ȷ���ȣ��Ե�ǰ֡��˵���൱����������Ϣ��������ڵ�ǰ֡�е������С���
			//��ǰ֡���յõ��Ĺ���ֵ��Ӧ�����������Χ�ڵģ���Ȼ��˵����һ֡�Ĺ���ֵ�ǱȽ����׵ģ�
			float z_inv_min = it->mu + sqrt(it->sigma2);					//�����ڵ�ǰ֡�е���������Ӧ��С��ȣ��˴��������е��ң�
			float z_inv_max = max(it->mu - sqrt(it->sigma2), 0.00000001f);	//�����ڵ�ǰ֡�е���С�����Ӧ������

			//z�����ߵĳ��ȣ�ע�ⲻ�������Ҳ���Ǳ����������������ȣ��������ߵķ������������õ��3D���ꣻ
			//ע��z���µ���Ȳ���ֵ�������it->mu����һ������˲��õ�����Ⱥ���ƽ����ȣ�
			//�µĲ���ֵ����һ�εĺ���ƽ����ȣ��ڵ�ǰ֡���൱����������Ϣ�ˣ������������Ƶ�ǰ֡���µĺ��������
			double z;
			//�µ���Ȳ���ֵz�ļ���ͨ��findEpipolarMatchDirect()�����
			if (!matcher_.findEpipolarMatchDirect(
				*it->ftr->frame, *frame, *it->ftr, 1.0 / it->mu, 1.0 / z_inv_min, 1.0 / z_inv_max, z))
			{
				it->b++; // increase outlier probability when no match was found�������ֱ�Ӽ�1
				++it;
				++n_failed_matches;
				continue;
			}

			// compute tau
			//������ȷ����ƽ��������׼���ע������ȵģ���������
			//�ο���REODE: Probabilistic, Monocular Dense Reconstruction in Real Time
			double tau = computeTau(T_ref_cur, it->ftr->f, z, px_error_angle);				//������ȵı�׼��

			//��������ȵı�׼�����õ�����������С�����������õ������С����֮�ȡ��ֵ������ı�׼���ȱ�׼��ĵ�����
			//tau_inverse�����׼��ǲ���ֵ�ı�׼����
			double tau_inverse = 0.5 * (1.0 / max(0.0000001, z - tau) - 1.0 / (z + tau));

			// update the estimate
			//�������Ӹ���
			//�ο���Video-based, Real-Time Multi View Stereo���䲹�����
			updateSeed(1. / z, tau_inverse*tau_inverse, &*it);

			++n_updates;										//���������û���õ�

			//�����ǰ����֡�ǹؼ�֡����ô�õ��Ӧ���ڵ�ǰ�ؼ�֡�������ڲ���Ҫ�������µ㣬����Ѿ������˵㣨���������û�е㣬
			//�����������㣬���������ӣ�
			if (frame->isKeyframe())
			{
				// The feature detector should not initialize new seeds close to this location
				feature_detector_->setGridOccpuancy(matcher_.px_cur_);
			}

			// if the seed has converged, we initialize a new candidate point and remove the seed
			//z_range�������ȣ�ע�ⲻ�����������������ȣ���it->sigma2����������ķ��seed_convergence_sigma2_thresh��ֵΪ200��
			//�����е��ң��������Ƚ��棬�����׻����ĸ�������ĸ�����ȣ�
			//��ؼ����ǣ�����������ж�׼���е㲻��ѧ��������ı�׼��С�������ȳ���һ����ֵ��ʲô������������߶�������ɣ���ôһ��
			//�����һ������ȣ���Ȼ������ָ����ֵ�����Ǿ��÷ǳ��Ĳ�����
			//������ķ�ʽҲ��Ӧ��������ʽ�жϹ��򣬿������Ÿĸ�
			if (sqrt(it->sigma2) < it->z_range / options_.seed_convergence_sigma2_thresh)
			{
				assert(it->ftr->point == NULL); // TODO this should not happen anymore

				//���ص��Ӧ���µ��������꣺it->mu�Ѿ����µ������ֵ��
				Vector3d xyz_world(it->ftr->frame->T_f_w_.inverse() * (it->ftr->f * (1.0 / it->mu)));

				//���������ӳ�Ϊ��ͼ��
				Point* point = new Point(xyz_world, it->ftr);
				it->ftr->point = point;
				/* FIXME it is not threadsafe to add a feature to the frame here.
				if(frame->isKeyframe())
				{
				  Feature* ftr = new Feature(frame.get(), matcher_.px_cur_, matcher_.search_level_);
				  ftr->point = point;
				  point->addFrameRef(ftr);
				  frame->addFeature(ftr);
				  it->ftr->frame->addFeature(it->ftr);
				}
				else
				*/

				//��FrameHandlerMono::initialize()�����Ѿ���seed_converged_cb�󶨵���ͼ�к�ѡ�ؼ���ĳ�Ա����newCandidatePoint()��
				//�ú������ǽ���ͼ��point���뵽��ѡ��ͼ��candidates_�б���
				{
					//��һ������Ϊ��ͼ�㣬�ڶ�������Ϊ�õ�ͼ��ķ�����ȷ���ȣ�
					//������Ҫ��Boost���﷨֪ʶ
					seed_converged_cb_(point, it->sigma2); // put in candidate list
				}
				//���������ĵ㣬����������б���ɾ�������ٵ���
				it = seeds_.erase(it);
			}
			//�������̫����Ϊ�䷢ɢ��ͬ��ɾ����
			else if (isnan(z_inv_min))
			{
				SVO_WARN_STREAM("z_min is NaN");
				it = seeds_.erase(it);
			}
			//��ȴ�������������˵���õ㻹��Ҫ�������е�������δ����
			else
				++it;
		}
	}

	void DepthFilter::clearFrameQueue()
	{
		while (!frame_queue_.empty())
			frame_queue_.pop();
	}

	void DepthFilter::getSeedsCopy(const FramePtr& frame, std::list<Seed>& seeds)
	{
		lock_t lock(seeds_mut_);
		for (std::list<Seed>::iterator it = seeds_.begin(); it != seeds_.end(); ++it)
		{
			if (it->ftr->frame == frame.get())
				seeds.push_back(*it);
		}
	}

	//����ĳ�����ӣ��������Ӻ���ֲ����ĸ�����a,b,mu,sigma2
	//�ο���Video-based, Real-Time Multi View Stereo���䲹�����
	//���룺	x	�����ܳ��ȵĵ�����������
	//			tau2	�����
	//			seed	����ָ��
	void DepthFilter::updateSeed(const float x, const float tau2, Seed* seed)
	{
		float norm_scale = sqrt(seed->sigma2 + tau2);
		if (std::isnan(norm_scale))
			return;

		//����boost�⣬����һ����ֵΪseed->mu����׼��Ϊnorm_scale����̬�ֲ�
		boost::math::normal_distribution<float> nd(seed->mu, norm_scale);

		float s2 = 1. / (1. / seed->sigma2 + 1. / tau2);		//sƽ����������ϣ�19��ʽ
		float m = s2*(seed->mu / seed->sigma2 + x / tau2);		//mֵ��������ϣ�20��ʽ

		float C1 = seed->a / (seed->a + seed->b) * boost::math::pdf(nd, x);	//������ϣ�21��ʽ
		float C2 = seed->b / (seed->a + seed->b) * 1. / seed->z_range;		//������ϣ�22��ʽ

		float normalization_constant = C1 + C2;								//���ǲ��������û�еģ���C1��C2���й�һ������
		C1 /= normalization_constant;
		C2 /= normalization_constant;

		float f = C1*(seed->a + 1.) / (seed->a + seed->b + 1.) + C2*seed->a / (seed->a + seed->b + 1.);		//�������ʽ��25��
		float e = C1*(seed->a + 1.)*(seed->a + 2.) / ((seed->a + seed->b + 1.)*(seed->a + seed->b + 2.))	//�������ʽ��26��
			+ C2*seed->a*(seed->a + 1.0f) / ((seed->a + seed->b + 1.0f)*(seed->a + seed->b + 2.0f));

		// update parameters
		float mu_new = C1*m + C2*seed->mu;														//�������ʽ��23�����µľ�ֵ
		seed->sigma2 = C1*(s2 + m*m) + C2*(seed->sigma2 + seed->mu*seed->mu) - mu_new*mu_new;	//�µķ���������ʽ��24��
		seed->mu = mu_new;
		seed->a = (e - f) / (f - e / f);		//���ݲ������ʽ��25������26�������µ�a��b����
		seed->b = seed->a*(1.0f - f) / f;
	}

	//������ȷ����ƽ��������׼���ע������ȵģ���������
	//�ο���REODE: Probabilistic, Monocular Dense Reconstruction in Real Time
	//���룺	z	���ߵ��ܳ��ȣ��������ߵķ����������ɵ�3D���꣨������Ȳ���ֵ��
	//			px_error_angle	��ǰ֡�е�������߽ǣ����ں��������
	//���أ�	tau		�����ƽ��������׼�
	double DepthFilter::computeTau(
		const SE3& T_ref_cur,
		const Vector3d& f,
		const double z,
		const double px_error_angle)
	{
		Vector3d t(T_ref_cur.translation());
		Vector3d a = f*z - t;		//f*z��ȡ��ʵ3D����
		double t_norm = t.norm();
		double a_norm = a.norm();
		double alpha = acos(f.dot(t) / t_norm); // dot product
		double beta = acos(a.dot(-t) / (t_norm*a_norm));	// dot product
		double beta_plus = beta + px_error_angle;			//ʽ��16��
		double gamma_plus = PI - alpha - beta_plus; // triangle angles sum to PI
		double z_plus = t_norm*sin(beta_plus) / sin(gamma_plus); // law of sines
		return (z_plus - z); // tau
	}

} // namespace svo
