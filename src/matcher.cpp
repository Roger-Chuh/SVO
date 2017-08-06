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

#include <cstdlib>
#include <vikit/abstract_camera.h>
#include <vikit/vision.h>
#include <vikit/math_utils.h>
#include <vikit/patch_score.h>
#include <svo/matcher.h>
#include <svo/frame.h>
#include <svo/feature.h>
#include <svo/point.h>
#include <svo/config.h>
#include <svo/feature_alignment.h>

namespace svo {

	namespace warp {

		//�������任����
		//ԭ���Բο��ؼ�֡�ϵ�ĳһ���Ϊ���ģ���Ϊr1������ȡ�����㣨��Ϊr2,r3��������һ��ֱ�������Σ�������������ұߺ����±�ȡ�������㣩��
		//Ȼ����ͶӰ����ǰ֡�У��õ����������꣬��ο�֡�е�һһ��Ӧ���ֱ�Ϊc1,c2,c3��������r2,r3���r1ֻ��һά�ϵ��˶����˶��ľ��붼Ϊ
		//�̶�������halfpatch_size�����ڴ�С��һ�룩������c2,c3�ֱ��ȥc1����halfpatch_size���ɵòο�֡���ؿ�任����ǰ֡�����ŵĳ߶ȣ����
		//ֻ�����죬��ôc2,c3��ȥc1���õĽ������һ��Ϊ0������һ�㶼�����һЩ��ת�ģ��ʶ����������죬������ת�Ĳ��������յõ���A_cur_ref����
		//���Խ���Ԫ�ؽӽ�1����������Ԫ�ز�����0�����ӽ�0����Ϊ��ת�����Ĳ���
		//Ҫǿ�����ǣ����������A_cur_ref�ǴӲο�֡ĳһ��ͼ�����������ڲ㣩����ǰ֡�ײ�ͼ��ķ���任���󣬶�����
		//�ο�֡�ײ�ͼ���뵱ǰ֡�ײ�ͼ��֮��ķ���任���󣻶�A_cur_refȡ�棬�Ϳ��Եõ��ӵ�ǰ֡�ײ�ͼ��任���ο�֡
		//����������ڲ�ͼ��ķ������
		//ȡ��ʱ�����Ǻ��ڵײ�ͼ����ȡ8*8(��10*10)�������򣬶����ڶ�Ӧ��ͼ����ȡ8*8(��10*10)�ķ�������
		//���ǿ��һ�£��˴��ķ���������������Ӧ�ģ��������������㶼������ͬ�ķ�����󣬶���ÿ�������㶼���Լ���
		//һ���ֲ�����任����
		void getWarpMatrixAffine(
			const vk::AbstractCamera& cam_ref,	//�ο��ؼ�֡��������ο��ؼ�֡���������
			const vk::AbstractCamera& cam_cur,	//��ǰ֡���������ǰ֡���������
			const Vector2d& px_ref,				//����ڲο��ؼ�֡����������㣩�ײ������ͼ���ϵ�����
			const Vector3d& f_ref,				//�ο�֡�۲�õ�����߷�������
			const double depth_ref,				//�ڲο�֡�е�ƽ������
			const SE3& T_cur_ref,				//�ο�֡����ǰ֡��SE3����
			const int level_ref,				//���������ڲ㼶���ο��ؼ�֡�еĲ㼶��
			Matrix2d& A_cur_ref)				//���յļ��������������
		{
			// Compute affine warp matrix A_ref_cur
			const int halfpatch_size = 5;

			//ʵ�ַ��������ȣ��õ������3D���꣬Ҫע����ǣ�depth_ref�����ߵĳ��ȣ�����z�����ֵ��
			//�����У������ָ���߾��룬���Ǵ�ֱ��ƽ��ľ���
			const Vector3d xyz_ref(f_ref*depth_ref);

			//�ڲο��ؼ�֡����ȡ�����㣬ÿ�������һ����������λ�ƣ�Ȼ����������ͶӰ�����������У�
			//ע�������level_ref��˵�������������������ڲ�ȡһ��halfpatch_size��������任���ײ㣬�䳤����������
			Vector3d xyz_du_ref(cam_ref.cam2world(px_ref + Vector2d(halfpatch_size, 0)*(1 << level_ref)));	//����x��������ƫ����5������
			Vector3d xyz_dv_ref(cam_ref.cam2world(px_ref + Vector2d(0, halfpatch_size)*(1 << level_ref)));	//����y��������ƫ����5������

			//�������Ӧ�����������������xyz_du_ref / xyz_du_ref[2]�����ο��ؼ�֡����Ӧ��3D�����һ�����õ���һ��ͼ�����ꣻ
			//Ȼ�����xyz_ref[2]�õ��������3D����
			//����һ�£��������һ֡���������֪�ģ���������ȡ�������㣬�������δ֪�ģ�Ϊ���ܹ�������ȡ��������任����ǰ֡�������ϵ�У�
			//������Ҫ�ָ�������������ϵ�е����꣬�����Ҫ��������ȣ�����ô�����ˣ���Ϊ�������������㲢��Զ����ˣ���Ϊ��������z����
			//��ͬ��ע�ⲻ��ʹ�����߳�����ͬ��
			xyz_du_ref *= xyz_ref[2] / xyz_du_ref[2];
			xyz_dv_ref *= xyz_ref[2] / xyz_dv_ref[2];

			//����ȡ����������任����ǰ֡�������ϵ��
			const Vector2d px_cur(cam_cur.world2cam(T_cur_ref*(xyz_ref)));
			const Vector2d px_du(cam_cur.world2cam(T_cur_ref*(xyz_du_ref)));
			const Vector2d px_dv(cam_cur.world2cam(T_cur_ref*(xyz_dv_ref)));

			//��ǰ֡�еĵ���������ڹؼ�֡�е�λ���������ɵ���������
			//ע�⣬���Ĳ���halfpatch_size*(1 << level_ref)������halfpatch_size�������ռ���Ĳ��ǵ�ǰ֡�ײ�ͼ����
			//�ο�֡�ײ�ͼ��ķ���任���󣬶��ǵ�ǰ֡�ײ�ͼ�񵽲ο�֡���������ڲ�ͼ��ķ���任����
			A_cur_ref.col(0) = (px_du - px_cur) / halfpatch_size;
			A_cur_ref.col(1) = (px_dv - px_cur) / halfpatch_size;
		}

		//���ݷ����������ʽ��ֵ���������߶ȣ�Ҳ���Ǹ������㣬���п��ܳ����ڵ�ǰ֡����һ��ͼ����
		int getBestSearchLevel(
			const Matrix2d& A_cur_ref,		//���������Ӧ�Ĳο�֡���������ڲ�ͼ�񵽵�ǰ֡�ײ�ͼ��ķ������
			const int max_level)			//������������
		{
			// Compute patch level in other image
			int search_level = 0;
			double D = A_cur_ref.determinant();
			while (D > 3.0 && search_level < max_level)
			{
				search_level += 1;
				D *= 0.25;
			}
			return search_level;
		}

		//�ڵ�ǰ֡��ȡһ���������򣬶��������з���任���õ��ο�֡�����򣬲�ͨ����ֵ��ȡ��Щ���������ֵ
		void warpAffine(
			const Matrix2d& A_cur_ref,		//�������㴦���ο��ؼ�֡����ǰ֡�ķ���任����
			const cv::Mat& img_ref,			//�ο��ؼ�֡ͼ�����������ڲ�ͼ��
			const Vector2d& px_ref,			//�������ڲο��ؼ�֡�ײ�ͼ���x����
			const int level_ref,			//�������ڲο��ؼ�֡�ײ�ͼ���y����
			const int search_level,			//�������ڵ�ǰ֡�����ڲ���
			const int halfpatch_size,		//������С��һ��+1��Ҳ��5��
			uint8_t* patch)					//һά���飬��СΪ10*10����Ӧ�ο�֡�ϵ�һ��10*10�����
		{
			//10*10����
			const int patch_size = halfpatch_size * 2;

			//�������㴦���ӵ�ǰ֡���ο�֡�ķ���任����
			const Matrix2f A_ref_cur = A_cur_ref.inverse().cast<float>();

			//A_ref_cur�������Խ����ϵ�Ԫ�أ�����Ԫ����ʵ�������½ӽ�0�����ȡ��õ��ľ���ĵ�һ��Ԫ�������
			//˵�����û�з���ƽ���˶������Ҫ����Ϊʲô���Ǿͱ���ȡ��һ��getWarpMatrixAffine()��������ô����A_cur_ref���ˡ�
			//�������������Ӧ���Ǻ��ٷ����ģ����ǣ������׵��ǣ�Ϊʲô����ֱ�ӷ����ˣ����ֱ�ӷ����ˣ���ôpatch_with_border_
			//��û��ֵ�������patch_with_border_��ȡ�ľ�����һ�������ܱߵ�����ֵ�ˣ���������𣨲���һ�������£�������뵽if�У�����
			//���⣬����ע����ֻ�ᵽƽ�ƣ���û�ᵽ��ת����ת����أ��������ִ����ǲ��Ǵ���һ���Ľ��Ұ�������
			if (isnan(A_ref_cur(0, 0)))
			{
				printf("Affine warp is NaN, probably camera has no translation\n"); // TODO
				return;
			}

			// Perform the warp on a larger patch.
			//��ȡpatch_���׵�ַ
			uint8_t* patch_ptr = patch;

			//�������ڲο�֡��Ӧ��ͼ���е�����
			const Vector2f px_ref_pyr = px_ref.cast<float>() / (1 << level_ref);
			
			//���Ϊ�У��ڲ�Ϊ�У���֪�����յõ���һάpatch_with_border_�ǰ��д洢��
			for (int y = 0; y < patch_size; ++y)
			{
				for (int x = 0; x < patch_size; ++x, ++patch_ptr)
				{
					//�ڵ�ǰ֡���������ڵ��������
					Vector2f px_patch(x - halfpatch_size, y - halfpatch_size);	

					//�������任����ǰ֡�ĵײ�ͼ���ϵ�����
					px_patch *= (1 << search_level);

					//A_ref_cur��ֱ�Ӵӵ�ǰ֡�ײ�ͼ��任���ο��ؼ�֡���������ڲ�ͼ��ķ���任�������getWarpMatrixAffine()������
					//��ǰ֡�з�����������������Է������õ��ο�֡�е�����������ĵ�������꣬�����������ڲο�֡��Ӧ��ͼ������꣬
					//�õ��ο�֡����ľ�������
					const Vector2f px(A_ref_cur*px_patch + px_ref_pyr);

					//�����Ƿ���ͼ�������ڣ��������ֱ��ȡ����ֵΪ0������ڣ�������˫���Բ�ֵ����õ�����ֵ
					if (px[0] < 0 || px[1] < 0 || px[0] >= img_ref.cols - 1 || px[1] >= img_ref.rows - 1)
						*patch_ptr = 0;
					else
						*patch_ptr = (uint8_t)vk::interpolateMat_8u(img_ref, px[0], px[1]);
				}
			}
		}

	} // namespace warp

	//���ǻ��õ���ȣ����������ȵķ���������˼����ǿ��һ�Σ��������е���������ߵĳ��ȣ����������ƽ�洹ֱ��z���꣬
	//����Ҫʱ�̼�סһ����ǣ�������Ҫ��������ڲο�֡�е���ȣ��ڵ�ǰ֡�е�������ǲ������ģ�ֻ�����������²ο�֡�е���ȡ�
	//���ǻ�����󷨣������ڲο�֡���������Ϊdr���ڵ�ǰ֡���������Ϊdc���ڲο�֡���������ߵķ�������Ϊf_ref����
	//��ǰ֡���������߷�������Ϊf_cur���ο�֡֡����ǰ֡����ת����ΪR��ƽ��Ϊt����ô�����������R*f_ref*dr-f_cur*dc=t��
	//�����н�����д���˼Ӻţ��ⲻӰ������ֻ����õ�dc�Ǹ�ֵ���ѣ�ʹ��α�漴������dr,dc������dr��������Ҫ�ģ�
	//��ᷢ����ʵdr,dc�ľ���ֵ�ȽϽӽ����ǿ϶��𣬱Ͼ�������˶��ǱȽ�������
	bool depthFromTriangulation(
		const SE3& T_search_ref,		//�ο�֡����ǰ֡��SE3����
		const Vector3d& f_ref,			//����ڲο�֡�����ߵķ�������
		const Vector3d& f_cur,			//����ڵ�ǰ֡��ƥ������������
		double& depth)					//���ջָ��������
	{
		//T_search_ref��Sophus�е��࣬��rotation_matrix()ת��ΪEigen�࣬�ٳ���
		Matrix<double, 3, 2> A; A << T_search_ref.rotation_matrix() * f_ref, f_cur;
		const Matrix2d AtA = A.transpose()*A;
		if (AtA.determinant() < 0.000001)
			return false;

		//����α��������
		const Vector2d depth2 = -AtA.inverse()*A.transpose()*T_search_ref.translation();

		//��һ�����ֵ���ǲο��ؼ�֡���������
		depth = fabs(depth2[0]);
		return true;
	}

	//�Ӵ��߽��ͼ�������ȡ�������߽��ͼ��飨��ν���߽��ͼ������10*10ͼ��飬�������õ���8*8��ͼ��飩
	//�ڴ�֮ǰ�Ѿ�ͨ��warpAffine()����õ�ref_patch_border_ptr��ֵ
	void Matcher::createPatchFromPatchWithBorder()
	{
		//patch_size_�Ǿ�̬��ֵ��������match.h�и�ֵΪ8
		//��ȡpatch_�׵�ַ������������ָ��
		uint8_t* ref_patch_ptr = patch_;

		//patch_with_border�ܹ�10��10��
		//ֱ�Ӵ�1��ʼ����ֹ��8��������ȡ��2�е���9��֮��Ŀ�
		for (int y = 1; y < patch_size_ + 1; ++y, ref_patch_ptr += patch_size_)
		{
			//����һ��1���Լ�����ѭ����ֹ��7��Ҳ��������ȡ��2�е���9���еĿ�
			uint8_t* ref_patch_border_ptr = patch_with_border_ + y*(patch_size_ + 2) + 1;
			
			//x��0��ʼ����Ϊ�����Ѿ�����һ��1��ref_patch_border_ptr�Ѿ�����ÿ�е��׵�ַ������ÿ�еĵڶ���Ԫ�صĵ�ַ
			//��ֹ��7��Ҳ���Ǵ�ÿ�еڶ�����ַ������ƫ��7����ַ������ȡ����9��
			for (int x = 0; x < patch_size_; ++x)
				ref_patch_ptr[x] = ref_patch_border_ptr[x];
		}
	}

	bool Matcher::findMatchDirect(
		const Point& pt,
		const Frame& cur_frame,
		Vector2d& px_cur)
	{
		if (!pt.getCloseViewObs(cur_frame.pos(), ref_ftr_))
			return false;

		if (!ref_ftr_->frame->cam_->isInFrame(
			ref_ftr_->px.cast<int>() / (1 << ref_ftr_->level), halfpatch_size_ + 2, ref_ftr_->level))
			return false;

		// warp affine
		warp::getWarpMatrixAffine(
			*ref_ftr_->frame->cam_, *cur_frame.cam_, ref_ftr_->px, ref_ftr_->f,
			(ref_ftr_->frame->pos() - pt.pos_).norm(),
			cur_frame.T_f_w_ * ref_ftr_->frame->T_f_w_.inverse(), ref_ftr_->level, A_cur_ref_);
		search_level_ = warp::getBestSearchLevel(A_cur_ref_, Config::nPyrLevels() - 1);
		warp::warpAffine(A_cur_ref_, ref_ftr_->frame->img_pyr_[ref_ftr_->level], ref_ftr_->px,
			ref_ftr_->level, search_level_, halfpatch_size_ + 1, patch_with_border_);
		createPatchFromPatchWithBorder();

		// px_cur should be set
		Vector2d px_scaled(px_cur / (1 << search_level_));

		bool success = false;
		if (ref_ftr_->type == Feature::EDGELET)
		{
			Vector2d dir_cur(A_cur_ref_*ref_ftr_->grad);
			dir_cur.normalize();
			success = feature_alignment::align1D(
				cur_frame.img_pyr_[search_level_], dir_cur.cast<float>(),
				patch_with_border_, patch_, options_.align_max_iter, px_scaled, h_inv_);
		}
		else
		{
			success = feature_alignment::align2D(
				cur_frame.img_pyr_[search_level_], patch_with_border_, patch_,
				options_.align_max_iter, px_scaled);
		}
		px_cur = px_scaled * (1 << search_level_);
		return success;
	}


	//���ż��������ο�֡���������ڵ�ǰ֡�е�ƥ��㣻
	//���������С��ȣ��õ��ڵ�ǰ֡�еļ��߶Σ����Ÿü��߶�����ƥ��㣻
	//��������ҵ�ƥ��㣬����true�����õ�depth
	bool Matcher::findEpipolarMatchDirect(
		const Frame& ref_frame,				//�ο��ؼ�֡
		const Frame& cur_frame,				//��ǰ֡
		const Feature& ref_ftr,				//�ο�֡�е�������
		const double d_estimate,			//��һ֡���Ƶõ�����Ⱦ�ֵ������ĵ�����
		const double d_min,					//��С��ȣ��������ĵ�����
		const double d_max,					//�����ȣ���С����ĵ�����
		double& depth)						//����õ�����ȣ����ƥ��ɹ��Ļ�������㲢�������ֵ��
	{
		//�Ӳο�֡����ǰ֡��SE3����
		SE3 T_cur_ref = cur_frame.T_f_w_ * ref_frame.T_f_w_.inverse();
		int zmssd_best = PatchScore::threshold();		//����ʱ����ӡ��PatchScore::threshold()��ֵΪ128000
		Vector2d uv_best;		//���ƥ��㣨��һ��ƽ���ϵ����꣬������ά�����Ϊ1����һ��ͼ�����꣩

		// Compute start and end of epipolar line in old_kf for match search, on unit plane!���ڵ�λƽ���ϣ�
		//���߷�����������С��ȵõ�����ڲο�֡���������ϵ�е���Զ���3D���꣬Ȼ���ٳ��Բο�֡����ǰ֡��SE3����
		//����ڵ�ǰ֡�������ϵ�������Զ3D���꣬���ͶӰ����ǰ֡ͼ���ϣ��õ��ڵ�ǰ֡���ߵ����˵��������꣨project2d����
		//�ǳ��Ե���ά���꣬�൱���ǵõ���һ��ͼ�����꣩
		Vector2d A = vk::project2d(T_cur_ref * (ref_ftr.f*d_min));		//�����
		Vector2d B = vk::project2d(T_cur_ref * (ref_ftr.f*d_max));		//��Զ��
		epi_dir_ = A - B;

		// Compute affine warp matrix��A_cur_ref
		//���ݵ�ǰ֡�Ͳο�֡�Լ���ǰ���ǵ�������������任������Ҫ�����ű任��Ҳ����ת��
		//��Ϊ�ǵ�ǰ֡��ؼ�֮֡��Ĺ�ϵ��������Ҫ���Ƿ���任�ģ�
		//��ȡ��A_cur_ref�ǲο�֡ĳһ��ͼ�����������ڲ㣩����ǰ֡�ײ�ͼ��ķ���任����
		warp::getWarpMatrixAffine(
			*ref_frame.cam_, *cur_frame.cam_, ref_ftr.px, ref_ftr.f,
			d_estimate, T_cur_ref, ref_ftr.level, A_cur_ref_);

		// feature pre-selection
		//ֱ�������������õ�ֱ��������
		reject_ = false;
		if (ref_ftr.type == Feature::EDGELET && options_.epi_search_edgelet_filtering)
		{
			const Vector2d grad_cur = (A_cur_ref_ * ref_ftr.grad).normalized();
			const double cosangle = fabs(grad_cur.dot(epi_dir_.normalized()));
			if (cosangle < options_.epi_search_edgelet_max_angle) {
				reject_ = true;
				return false;
			}
		}

		//��ȡ��������߶ȣ�Ҳ�����������ڵ�ǰ֡Ӧ����ô�����һ��ͼ���У���ORB-SLAM��Ҳ�����Ƶģ������������ڵ�ǰ֡���ڲ㣩��
		//���ݲο�֡�뵱ǰ֮֡��ķ���任������ʽ�������������ڵ�ǰ֡�е����ڲ�
		search_level_ = warp::getBestSearchLevel(A_cur_ref_, Config::nPyrLevels() - 1);

		// Find length of search range on epipolar line
		//A�ǵ�ǰ֡�������ϵ�µĹ�һ��ͼ�����꣬����ʹ��world2cam������һ������任���������꣨�����ڲξ���
		Vector2d px_A(cur_frame.cam_->world2cam(A));
		Vector2d px_B(cur_frame.cam_->world2cam(B));

		//�����ڲο�֡�е������С�����õ��ڵ�ǰ֡�еļ��߳��ȣ����ص�λ
		epi_length_ = (px_A - px_B).norm() / (1 << search_level_);

		// Warp reference patch at ref_level
		//ע��ʵ���õ���8*8������飬���ú�������õ���patch_with_border_��һ��10*10������飨��˵�Ƕ�ά�ģ����ǰ��д洢Ϊ
		//һά����patch_with_border_��������ΪʲôҪȡ10*10���������ȡ�����˸о�û��ʲô��Ҫ������ֱ�Ӽ���һ��8*8������
		//���⣬��Ȼ����getWarpMatrixAffine()������ǴӲο�֡����ǰ֡�ķ�����󣬵�ʵ���ϣ������Ե�ǰ֡��Ϊ�ο��ģ�Ҳ�����õ�ʱ��
		//����A_cur_ref_�����󣬵�ǰ֡�е������Ƿ��εģ����ο��ؼ�֡�������ǲ�ȷ���ģ�����patch_with_border_�洢��10*10���ο�֡��
		//������ֵ��
		//warpAffine()�ڵ�ǰ֡��ȡ��һ��10*10������������մ�1��10��˳���ŵĻ�����ô�������ڵ�6λ��Ҳ�������ڼ���õ�����һ��ż��
		//��������������ߣ��ϱߣ���5�����أ��ұߣ��±ߣ���4�����أ�������ȡ8*8����Ҳ����������ȡ��������������ߣ��ϱߣ���4�����أ�
		//�ұߣ��±ߣ���3�����أ�
		//���⣬��ס����������ȡ����������������ߣ��ϱߣ���4�����أ����ұ���3�����أ����������ڵ�ǰ֡����ȡʱ��ҲҪ��֮��Ӧ��
		//ʵ���ϣ�����ʹ����UZH rpgʵ�����Լ��Ŀ�rpg_vikit��ʵ�ʲ�û����ȡ����������ֻ�ǽ�������׵�ַ����Ϳ����ˣ�����׵�ַ�ļ���
		//��Ӧ�ϾͿ����ˡ�
		warp::warpAffine(A_cur_ref_, ref_frame.img_pyr_[ref_ftr.level], ref_ftr.px,
			ref_ftr.level, search_level_, halfpatch_size_ + 1, patch_with_border_);

		//patch_���ο��ؼ�֡��ͼ��������ڸú����б���ֵ��patch_with_border_�������warpAffine()�����б���ֵ����ߴ�Ϊ10*10��
		//��ʵ�������õ���8*8�ĳߴ磬���Խ������Ӹ�10*10������У��ٳ�8*8�����򣬼���partch_with_border_�л�ȡpatch_
		//�ú�������ʱ�����ȡ�����������κ��޸�
		createPatchFromPatchWithBorder();

		//����������߹��̣���С��2�������ˣ���ô��ֱ�ӿ��Ǽ��ߵ��е㣨����һ��һ�������ˣ�
		if (epi_length_ < 2.0)
		{
			px_cur_ = (px_A + px_B) / 2.0;

			//��ǰ֡������任����Ӧ�������ͼ���ϵ�����
			Vector2d px_scaled(px_cur_ / (1 << search_level_));

			bool res;

			//��ȷ�������أ�����KLT���������٣���ȷ�������ؾ��ȣ�
			//ע��px_scaled�Ѿ��Ǳ任����Ӧ�������ͼ���ϵ����꣬����align2D��align1Dͼ��ʱ��Ҳ�������Ӧ�������ͼ��
			if (options_.align_1d)
				res = feature_alignment::align1D(
					cur_frame.img_pyr_[search_level_], (px_A - px_B).cast<float>().normalized(),
					patch_with_border_, patch_, options_.align_max_iter, px_scaled, h_inv_);
			else
				res = feature_alignment::align2D(
					cur_frame.img_pyr_[search_level_], patch_with_border_, patch_,
					options_.align_max_iter, px_scaled);
			if (res)
			{
				//�任�ص�ǰ֡�ײ�ͼ���ϵ�����
				px_cur_ = px_scaled*(1 << search_level_);
				//���ǻ��������
				if (depthFromTriangulation(T_cur_ref, ref_ftr.f, cur_frame.cam_->cam2world(px_cur_), depth))
					return true;
			}
			return false;
		}

		//epi_length/0.7�õ����������������缫���ܳ�70�����أ���ô����������Ϊ100�Σ�ÿ�β���Ϊ0.7�����أ�
		size_t n_steps = epi_length_ / 0.7; // one step per pixel

		//�����ڹ�һ��ƽ���ϵĳ��ȳ��Ե����������õ�ÿ�����������ߵ�ƫ������ע���ǹ�һ��ƽ���ϵĵ�λ���������ص�λ��
		Vector2d step = epi_dir_ / n_steps;

		//max_epi_search_stepsÿ�����ڼ��������趨����������������matcher.h��ָ��ֵΪ1000��Ҳ��˵������������߹��������µ����������࣬
		//ʹ����������������ָ������������������ôֱ�ӷ����õ㣬��Ϊ�������߹������൱�������С�������̫�󣬲�ȷ����̫�󣩣����ֲ���Ҳ��
		//Ϊ�˱�֤�ٶȰ���
		if (n_steps > options_.max_epi_search_steps)
		{
			printf("WARNING: skip epipolar search: %zu evaluations, px_lenght=%f, d_min=%f, d_max=%f.\n",
				n_steps, epi_length_, d_min, d_max);
			return false;
		}

		// for matching, precompute sum and sum2 of warped reference patch
		int pixel_sum = 0;
		int pixel_sum_square = 0;

		//PatchScore�ǵ�������rgp_vikit�е�ģ���࣬�����ڼ���������֮���NCCֵ���������patch_�ǲο��ؼ�֡��ͼ���
		PatchScore patch_score(patch_);

		// now we sample along the epipolar line
		//��ʼ���ż����������ƥ���
		//�������ΪB�㣨�ڹ�һ��ƽ���Ͻ��е���������
		Vector2d uv = B - step;

		//��һ�����������꣨��ʼ����0��0����
		Vector2i last_checked_pxi(0, 0);
		++n_steps;
		for (size_t i = 0; i < n_steps; ++i, uv += step)
		{
			//������ת�����������꣨uv�ǹ�һ��ƽ���ϵ����꣩
			Vector2d px(cur_frame.cam_->world2cam(uv));

			//������������任����Ӧ��ͼ���ϵ�����
			//ע������Vector2i���ͣ�Ҳ�������͵ģ���������������ݣ���ôʼ�ջ�����ȡ����������Ϊ���ԭ�򣬲���Ҫ����0.5��
			//���������������Ч����ʹ��ȡ��Ϊ��ӽ���ֵ
			Vector2i pxi(px[0] / (1 << search_level_) + 0.5,
				px[1] / (1 << search_level_) + 0.5); // +0.5 to round to closest int

			if (pxi == last_checked_pxi)
				continue;
			//���ܻ���ֵ�ǰ�������뵱һ��������һ���������Ҳ���аɣ�������Щ���꾭����������֮��ȡ��֮�󣬻���������
			last_checked_pxi = pxi;

			// check if the patch is full within the new frame
			//��֤�ڵ�ǰ֡��ȡ8*8�飬�Ƿ�ȫ��ͼ��������
			if (!cur_frame.cam_->isInFrame(pxi, patch_size_, search_level_))
				continue;

			// TODO interpolation would probably be a good idea
			//��ȡ��ǰ֡8*8��ͼ����׵�ַ��.data��Mat���͵�����ͷ��Ҳ���Ǿ�����׵�ַ��
			//����(pxi[1] - halfpatch_size_)*cur_frame.img_pyr_[search_level_].cols
			//Ϊ����*�������ټ���(pxi[0] - halfpatch_size_)�õ����յĿ��׵�ַ��ע���Ǽ�ȥhalfpatch_size_=4��
			//��������������������ߣ��ϱߣ�����4��Ԫ�صģ��׵�ַ�ļ���Ҫ���������
			uint8_t* cur_patch_ptr = cur_frame.img_pyr_[search_level_].data
				+ (pxi[1] - halfpatch_size_)*cur_frame.img_pyr_[search_level_].cols
				+ (pxi[0] - halfpatch_size_);
			//�����֮��ĵ÷֣������֮���SSDֵ
			int zmssd = patch_score.computeScore(cur_patch_ptr, cur_frame.img_pyr_[search_level_].cols);

			//ͳ����С��SSDֵ
			if (zmssd < zmssd_best) {
				zmssd_best = zmssd;
				uv_best = uv;
			}
		}

		//������ƥ��SSDֵС��ָ��ֵ������ܸ�ƥ�䣨��ӡ���PatchScore::threshold()��ֵΪ128000��
		if (zmssd_best < PatchScore::threshold())
		{
			//subpix_refinement��matcher.h���Ѿ�����Ϊtrue
			//��ȷ�������ؾ���
			if (options_.subpix_refinement)
			{
				px_cur_ = cur_frame.cam_->world2cam(uv_best);

				//����ǰ֡�еײ�ͼ�������任��ָ���㣨search_level_�ϣ�ͼ����
				Vector2d px_scaled(px_cur_ / (1 << search_level_));
				bool res;
				if (options_.align_1d)
					res = feature_alignment::align1D(
						cur_frame.img_pyr_[search_level_], (px_A - px_B).cast<float>().normalized(),
						patch_with_border_, patch_, options_.align_max_iter, px_scaled, h_inv_);
				else
					res = feature_alignment::align2D(
						cur_frame.img_pyr_[search_level_], patch_with_border_, patch_,
						options_.align_max_iter, px_scaled);
				if (res)
				{
					//�任�صײ�ͼ�������
					px_cur_ = px_scaled*(1 << search_level_);
					
					//cam2world����������������һ����Ҳ���ǵõ�������������Ĺ�һ�����꣨����������
					if (depthFromTriangulation(T_cur_ref, ref_ftr.f, cur_frame.cam_->cam2world(px_cur_), depth))
						return true;
				}
				return false;
			}

			px_cur_ = cur_frame.cam_->world2cam(uv_best);

			//unprojected()�����ط��ع�һ���������꣬��˻���Ҫ���й�һ���õ���ǰ֡�е�������ߵķ�������
			if (depthFromTriangulation(T_cur_ref, ref_ftr.f, vk::unproject2d(uv_best).normalized(), depth))
				return true;
		}
		return false;
	}

} // namespace svo
