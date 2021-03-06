/* The copyright in this software is being made available under the BSD
 * Licence, included below.  This software may be subject to other third
 * party and contributor rights, including patent rights, and no such
 * rights are granted under this licence.
 *
 * Copyright (c) 2017-2018, ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * * Neither the name of the ISO/IEC nor the names of its contributors
 *   may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "BitReader.h"
#include "BitWriter.h"
#include "PCCMisc.h"
#include "hls.h"
#include "io_hls.h"

#include <iomanip>
#include <iterator>
#include <algorithm>
#include <sstream>

namespace pcc {

//============================================================================

Oid::operator std::string() const
{
  std::stringstream ss;
  int subidentifier = 0;
  int firstSubidentifier = 1;
  for (auto byte : this->contents) {
    if (byte & 0x80) {
      subidentifier = (subidentifier << 7) | (byte & 0x7f);
      continue;
    }

    // end of subidentifer.
    // NB: the first subidentifier encodes two oid components
    if (firstSubidentifier) {
      firstSubidentifier = 0;
      if (subidentifier < 40)
        ss << '0';
      else if (subidentifier < 80) {
        ss << '1';
        subidentifier -= 40;
      } else {
        ss << '2';
        subidentifier -= 80;
      }
    }

    ss << '.' << std::to_string(subidentifier);
    subidentifier = 0;
  }

  return ss.str();
}

//----------------------------------------------------------------------------

bool
operator==(const Oid& lhs, const Oid& rhs)
{
  // NB: there is a unique encoding for each OID.  Equality may be determined
  // comparing just the content octets
  return lhs.contents == rhs.contents;
}

//----------------------------------------------------------------------------

template<typename T>
void
writeOid(T& bs, const Oid& oid)
{
  // write out the length according to the BER definite shoft form.
  // NB: G-PCC limits the length to 127 octets.
  constexpr int oid_reserved_zero_bit = 0;
  int oid_len = oid.contents.size();
  bs.writeUn(1, oid_reserved_zero_bit);
  bs.writeUn(7, oid_len);

  const auto& oid_contents = oid.contents;
  for (int i = 0; i < oid_len; i++)
    bs.writeUn(8, oid_contents[i]);
}

//----------------------------------------------------------------------------

template<typename T>
void
readOid(T& bs, Oid* oid)
{
  int oid_reserved_zero_bit;
  int oid_len;
  bs.readUn(1, &oid_reserved_zero_bit);
  bs.readUn(7, &oid_len);
  oid->contents.resize(oid_len);
  for (int i = 0; i < oid_len; i++)
    bs.readUn(8, &oid->contents[i]);
}

//----------------------------------------------------------------------------

int
lengthOid(const Oid& oid)
{
  return 1 + oid.contents.size();
}

//============================================================================

std::ostream&
operator<<(std::ostream& os, const AttributeLabel& label)
{
  switch (label.known_attribute_label) {
  case KnownAttributeLabel::kColour: os << "color"; break;
  case KnownAttributeLabel::kReflectance: os << "reflectance"; break;
  case KnownAttributeLabel::kFrameIndex: os << "frame index"; break;
  case KnownAttributeLabel::kMaterialId: os << "material id"; break;
  case KnownAttributeLabel::kTransparency: os << "transparency"; break;
  case KnownAttributeLabel::kNormal: os << "normal"; break;
  case KnownAttributeLabel::kOid: os << std::string(label.oid);
  default:
    // An unknown known attribute
    auto iosFlags = os.flags(std::ios::hex);
    os << std::setw(8) << label.known_attribute_label;
    os.flags(iosFlags);
  }

  return os;
}

//============================================================================

template<typename Bs>
void
writeAttrParamCicp(Bs& bs, const AttributeDescription& param)
{
  bs.writeUe(param.cicp_colour_primaries_idx);
  bs.writeUe(param.cicp_transfer_characteristics_idx);
  bs.writeUe(param.cicp_matrix_coefficients_idx);
  bs.write(param.cicp_video_full_range_flag);
  bs.byteAlign();
}

//----------------------------------------------------------------------------

template<typename Bs>
void
parseAttrParamCicp(Bs& bs, AttributeDescription* param)
{
  bs.readUe(&param->cicp_colour_primaries_idx);
  bs.readUe(&param->cicp_transfer_characteristics_idx);
  bs.readUe(&param->cicp_matrix_coefficients_idx);
  bs.read(&param->cicp_video_full_range_flag);
  param->cicpParametersPresent = 1;
  bs.byteAlign();
}

//============================================================================

template<typename Bs>
void
writeAttrParamScaling(Bs& bs, const AttributeDescription& param)
{
  bs.writeUe(param.source_attr_offset_log2);
  bs.writeUe(param.source_attr_scale_log2);
  bs.byteAlign();
}

//----------------------------------------------------------------------------

template<typename Bs>
void
parseAttrParamScaling(Bs& bs, AttributeDescription* param)
{
  bs.readUe(&param->source_attr_offset_log2);
  bs.readUe(&param->source_attr_scale_log2);
  param->scalingParametersPresent = 1;
  bs.byteAlign();
}

//============================================================================

template<typename Bs>
void
writeAttrParamDefaultValue(Bs& bs, const AttributeDescription& param)
{
  bs.writeUn(param.bitdepth, param.attr_default_value[0]);
  for (int k = 1; k <= param.attr_num_dimensions_minus1; k++)
    bs.writeUn(param.bitdepthSecondary, param.attr_default_value[k]);
  bs.byteAlign();
}

//----------------------------------------------------------------------------

template<typename Bs>
void
parseAttrParamDefaultValue(Bs& bs, AttributeDescription* param)
{
  param->attr_default_value.resize(param->attr_num_dimensions_minus1 + 1);

  bs.readUn(param->bitdepth, &param->attr_default_value[0]);
  for (int k = 1; k <= param->attr_num_dimensions_minus1; k++)
    bs.readUn(param->bitdepthSecondary, &param->attr_default_value[k]);
  bs.byteAlign();
}

//============================================================================

template<typename Bs>
void
writeAttrParamOpaque(Bs& bs, const OpaqueAttributeParameter& param)
{
  if (param.attr_param_type == AttributeParameterType::kItuT35) {
    bs.writeUn(8, param.attr_param_itu_t_t35_country_code);
    if (param.attr_param_itu_t_t35_country_code == 0xff)
      bs.writeUn(8, param.attr_param_itu_t_t35_country_code_extension);
  } else if (param.attr_param_type == AttributeParameterType::kOid)
    writeOid(bs, param.attr_param_oid);

  for (auto attr_param_byte : param.attr_param_byte)
    bs.writeUn(8, attr_param_byte);

  bs.byteAlign();
}

//----------------------------------------------------------------------------

template<typename Bs>
OpaqueAttributeParameter
parseAttrParamOpaque(
  Bs& bs, AttributeParameterType attr_param_type, int attrParamLen)
{
  bs.byteAlign();

  OpaqueAttributeParameter param;
  param.attr_param_type = attr_param_type;

  if (param.attr_param_type == AttributeParameterType::kItuT35) {
    bs.readUn(8, &param.attr_param_itu_t_t35_country_code);
    attrParamLen--;
    if (param.attr_param_itu_t_t35_country_code == 0xff) {
      bs.readUn(8, &param.attr_param_itu_t_t35_country_code_extension);
      attrParamLen--;
    }
  } else if (param.attr_param_type == AttributeParameterType::kOid) {
    readOid(bs, &param.attr_param_oid);
    attrParamLen -= lengthOid(param.attr_param_oid);
  }

  if (attrParamLen > 0) {
    param.attr_param_byte.resize(attrParamLen);
    for (int i = 0; i < attrParamLen; i++)
      bs.readUn(8, &param.attr_param_byte[i]);
  }

  return param;
}

//============================================================================

PayloadBuffer
write(const SequenceParameterSet& sps)
{
  PayloadBuffer buf(PayloadType::kSequenceParameterSet);
  auto bs = makeBitWriter(std::back_inserter(buf));

  bs.writeUn(24, sps.profileCompatibility.profile_compatibility_flags);
  bs.writeUn(8, sps.level);
  bs.writeUe(sps.sps_seq_parameter_set_id);

  bool seq_bounding_box_present_flag = true;
  bs.write(seq_bounding_box_present_flag);
  if (seq_bounding_box_present_flag) {
    auto sps_bounding_box_offset_xyz =
      toXyz(sps.geometry_axis_order, sps.seqBoundingBoxOrigin);

    bs.writeSe(sps_bounding_box_offset_xyz.x());
    bs.writeSe(sps_bounding_box_offset_xyz.y());
    bs.writeSe(sps_bounding_box_offset_xyz.z());

    int seq_bounding_box_offset_log2_scale = 0;
    bs.writeUe(seq_bounding_box_offset_log2_scale);

    auto seq_bounding_box_whd =
      toXyz(sps.geometry_axis_order, sps.seqBoundingBoxSize);

    bs.writeUe(seq_bounding_box_whd.x());
    bs.writeUe(seq_bounding_box_whd.y());
    bs.writeUe(seq_bounding_box_whd.z());
  }
  // todo(df): determine encoding of scale factor
  bs.writeF(sps.seq_geom_scale);
  bs.writeUn(1, sps.seq_geom_scale_unit_flag);

  int num_attribute_sets = int(sps.attributeSets.size());
  bs.writeUe(num_attribute_sets);
  for (const auto& attr : sps.attributeSets) {
    bs.writeUe(attr.attr_num_dimensions_minus1);
    bs.writeUe(attr.attr_instance_id);

    int attr_bitdepth_minus1 = attr.bitdepth - 1;
    bs.writeUe(attr_bitdepth_minus1);

    if (attr.attr_num_dimensions_minus1) {
      int attr_bitdepth_secondary_minus1 = attr.bitdepthSecondary - 1;
      bs.writeUe(attr_bitdepth_secondary_minus1);
    }

    const auto& label = attr.attributeLabel;
    bs.write(label.known_attribute_label_flag());
    if (label.known_attribute_label_flag())
      bs.writeUe(label.known_attribute_label);
    else
      writeOid(bs, label.oid);

    // Encode all of the attribute parameters.  The encoder works
    // in the fixed order descrbed here.  However this is non-normative.
    int num_attribute_parameters = attr.opaqueParameters.size();
    num_attribute_parameters += attr.cicpParametersPresent;
    num_attribute_parameters += attr.scalingParametersPresent;
    num_attribute_parameters += !attr.attr_default_value.empty();
    bs.writeUn(5, num_attribute_parameters);
    bs.byteAlign();

    if (!attr.attr_default_value.empty()) {
      int attr_param_len = 0;
      auto bsCounter = makeBitWriter(InsertionCounter(&attr_param_len));
      writeAttrParamDefaultValue(bsCounter, attr);

      auto attr_param_type = AttributeParameterType::kDefaultValue;
      bs.writeUn(8, attr_param_type);
      bs.writeUn(8, attr_param_len);
      writeAttrParamDefaultValue(bs, attr);
    }

    if (attr.cicpParametersPresent) {
      int attr_param_len = 0;
      auto bsCounter = makeBitWriter(InsertionCounter(&attr_param_len));
      writeAttrParamCicp(bsCounter, attr);

      auto attr_param_type = AttributeParameterType::kCicp;
      bs.writeUn(8, attr_param_type);
      bs.writeUn(8, attr_param_len);
      writeAttrParamCicp(bs, attr);
    }

    if (attr.scalingParametersPresent) {
      int attr_param_len = 0;
      auto bsCounter = makeBitWriter(InsertionCounter(&attr_param_len));
      writeAttrParamScaling(bsCounter, attr);

      auto attr_param_type = AttributeParameterType::kScaling;
      bs.writeUn(8, attr_param_type);
      bs.writeUn(8, attr_param_len);
      writeAttrParamScaling(bs, attr);
    }

    for (const auto& param : attr.opaqueParameters) {
      int attr_param_len = 0;
      auto bsCounter = makeBitWriter(InsertionCounter(&attr_param_len));
      writeAttrParamOpaque(bsCounter, param);

      bs.writeUn(8, param.attr_param_type);
      bs.writeUn(8, attr_param_len);
      writeAttrParamOpaque(bs, param);
    }
  }

  bs.writeUn(5, sps.log2_max_frame_idx);
  bs.writeUn(3, sps.geometry_axis_order);
  bs.write(sps.cabac_bypass_stream_enabled_flag);

  bool sps_extension_flag = false;
  bs.write(sps_extension_flag);
  bs.byteAlign();

  return buf;
}

//----------------------------------------------------------------------------

SequenceParameterSet
parseSps(const PayloadBuffer& buf)
{
  SequenceParameterSet sps;
  assert(buf.type == PayloadType::kSequenceParameterSet);
  auto bs = makeBitReader(buf.begin(), buf.end());

  bs.readUn(24, &sps.profileCompatibility.profile_compatibility_flags);
  bs.readUn(8, &sps.level);
  bs.readUe(&sps.sps_seq_parameter_set_id);

  bool seq_bounding_box_present_flag = bs.read();
  if (seq_bounding_box_present_flag) {
    Vec3<int> seq_bounding_box_offset;
    bs.readSe(&seq_bounding_box_offset.x());
    bs.readSe(&seq_bounding_box_offset.y());
    bs.readSe(&seq_bounding_box_offset.z());

    int seq_bounding_box_offset_log2_scale;
    bs.readUe(&seq_bounding_box_offset_log2_scale);
    seq_bounding_box_offset *= 1 << seq_bounding_box_offset_log2_scale;

    Vec3<int> seq_bounding_box_whd;
    bs.readUe(&seq_bounding_box_whd.x());
    bs.readUe(&seq_bounding_box_whd.y());
    bs.readUe(&seq_bounding_box_whd.z());

    // NB: these are in XYZ axis order until the SPS is converted to STV
    sps.seqBoundingBoxOrigin = seq_bounding_box_offset;
    sps.seqBoundingBoxSize = seq_bounding_box_whd;
  }
  bs.readF(&sps.seq_geom_scale);
  bs.readUn(1, &sps.seq_geom_scale_unit_flag);

  int num_attribute_sets = int(bs.readUe());
  for (int i = 0; i < num_attribute_sets; i++) {
    sps.attributeSets.emplace_back();
    auto& attr = sps.attributeSets.back();
    bs.readUe(&attr.attr_num_dimensions_minus1);
    bs.readUe(&attr.attr_instance_id);

    int attr_bitdepth_minus1;
    bs.readUe(&attr_bitdepth_minus1);
    attr.bitdepth = attr_bitdepth_minus1 + 1;

    if (attr.attr_num_dimensions_minus1) {
      int attr_bitdepth_secondary_minus1 = 0;
      bs.readUe(&attr_bitdepth_secondary_minus1);
      attr.bitdepthSecondary = attr_bitdepth_secondary_minus1 + 1;
    }

    auto& label = attr.attributeLabel;

    bool known_attribute_label_flag = bs.read();
    if (known_attribute_label_flag)
      bs.readUe(&label.known_attribute_label);
    else {
      label.known_attribute_label = KnownAttributeLabel::kOid;
      readOid(bs, &label.oid);
    }

    int num_attribute_parameters;
    bs.readUn(5, &num_attribute_parameters);
    bs.byteAlign();
    for (int i = 0; i < num_attribute_parameters; i++) {
      AttributeParameterType attr_param_type;
      int attr_param_len;
      bs.readUn(8, &attr_param_type);
      bs.readUn(8, &attr_param_len);
      // todo(df): check that all attr_param_len bytes are consumed
      switch (attr_param_type) {
        using Type = AttributeParameterType;
      case Type::kCicp: parseAttrParamCicp(bs, &attr); break;
      case Type::kScaling: parseAttrParamScaling(bs, &attr); break;
      case Type::kDefaultValue: parseAttrParamDefaultValue(bs, &attr); break;

      case Type::kItuT35:
      case Type::kOid:
      default:
        attr.opaqueParameters.emplace_back(
          parseAttrParamOpaque(bs, attr_param_type, attr_param_len));
      }
    }
  }

  bs.readUn(5, &sps.log2_max_frame_idx);
  bs.readUn(3, &sps.geometry_axis_order);
  bs.read(&sps.cabac_bypass_stream_enabled_flag);

  bool sps_extension_flag = bs.read();
  if (sps_extension_flag) {
    // todo(df): sps_extension_data;
    assert(!sps_extension_flag);
  }
  bs.byteAlign();

  return sps;
}

//----------------------------------------------------------------------------

void
convertXyzToStv(SequenceParameterSet* sps)
{
  // permute the bounding box from xyz to internal stv order
  sps->seqBoundingBoxOrigin =
    fromXyz(sps->geometry_axis_order, sps->seqBoundingBoxOrigin);

  sps->seqBoundingBoxSize =
    fromXyz(sps->geometry_axis_order, sps->seqBoundingBoxSize);
}

//============================================================================

PayloadBuffer
write(const SequenceParameterSet& sps, const GeometryParameterSet& gps)
{
  PayloadBuffer buf(PayloadType::kGeometryParameterSet);
  auto bs = makeBitWriter(std::back_inserter(buf));

  bs.writeUe(gps.gps_geom_parameter_set_id);
  bs.writeUe(gps.gps_seq_parameter_set_id);
  bs.write(gps.geom_box_log2_scale_present_flag);
  if (!gps.geom_box_log2_scale_present_flag)
    bs.writeUe(gps.gps_geom_box_log2_scale);
  bs.write(gps.predgeom_enabled_flag);
  bs.write(gps.geom_unique_points_flag);

  if (!gps.predgeom_enabled_flag) {
    bs.write(gps.qtbt_enabled_flag);
    bs.write(gps.neighbour_context_restriction_flag);
    bs.write(gps.inferred_direct_coding_mode_enabled_flag);
    bs.write(gps.bitwise_occupancy_coding_flag);
    bs.write(gps.adjacent_child_contextualization_enabled_flag);

    bs.write(gps.geom_planar_mode_enabled_flag);
    if (gps.geom_planar_mode_enabled_flag) {
      bs.writeUe(gps.geom_planar_threshold0);
      bs.writeUe(gps.geom_planar_threshold1);
      bs.writeUe(gps.geom_planar_threshold2);
      bs.writeUe(gps.geom_planar_idcm_threshold);
    }

    bs.write(gps.geom_angular_mode_enabled_flag);
    if (gps.geom_angular_mode_enabled_flag) {
      auto geom_angular_origin =
        toXyz(sps.geometry_axis_order, gps.geomAngularOrigin);
      bs.writeUe(geom_angular_origin.x());
      bs.writeUe(geom_angular_origin.y());
      bs.writeUe(geom_angular_origin.z());
      bs.writeUe(gps.geom_angular_num_lidar_lasers());

      if (gps.geom_angular_num_lidar_lasers()) {
        bs.writeSe(gps.geom_angular_theta_laser[0]);
        bs.writeSe(gps.geom_angular_z_laser[0]);
        bs.writeUe(gps.geom_angular_num_phi_per_turn[0]);
      }

      for (int i = 1; i < gps.geom_angular_num_lidar_lasers(); i++) {
        int geom_angular_theta_laser_diff = gps.geom_angular_theta_laser[i]
          - gps.geom_angular_theta_laser[i - 1];

        int geom_angular_z_laser_diff =
          gps.geom_angular_z_laser[i] - gps.geom_angular_z_laser[i - 1];

        // NB: angles must be in increasing monotonic order
        assert(geom_angular_theta_laser_diff >= 0);
        bs.writeUe(geom_angular_theta_laser_diff);
        bs.writeSe(geom_angular_z_laser_diff);
        bs.writeUe(gps.geom_angular_num_phi_per_turn[i]);
      }
      bs.write(gps.planar_buffer_disabled_flag);
    }

    bs.writeUe(gps.neighbour_avail_boundary_log2);
    bs.writeUe(gps.intra_pred_max_node_size_log2);
    bs.writeUe(gps.trisoup_node_size_log2);
    bs.write(gps.geom_scaling_enabled_flag);
    if (gps.geom_scaling_enabled_flag) {
      bs.writeUe(gps.geom_base_qp);
      bs.writeSe(gps.geom_idcm_qp_offset);
    }
  }

  bool gps_extension_flag = false;
  bs.write(gps_extension_flag);
  bs.byteAlign();

  return buf;
}

//----------------------------------------------------------------------------

GeometryParameterSet
parseGps(const PayloadBuffer& buf)
{
  GeometryParameterSet gps;
  assert(buf.type == PayloadType::kGeometryParameterSet);
  auto bs = makeBitReader(buf.begin(), buf.end());

  bs.readUe(&gps.gps_geom_parameter_set_id);
  bs.readUe(&gps.gps_seq_parameter_set_id);
  bs.read(&gps.geom_box_log2_scale_present_flag);
  if (!gps.geom_box_log2_scale_present_flag)
    bs.readUe(&gps.gps_geom_box_log2_scale);
  bs.read(&gps.predgeom_enabled_flag);
  bs.read(&gps.geom_unique_points_flag);

  if (!gps.predgeom_enabled_flag) {
    bs.read(&gps.qtbt_enabled_flag);
    bs.read(&gps.neighbour_context_restriction_flag);
    bs.read(&gps.inferred_direct_coding_mode_enabled_flag);
    bs.read(&gps.bitwise_occupancy_coding_flag);
    bs.read(&gps.adjacent_child_contextualization_enabled_flag);

    bs.read(&gps.geom_planar_mode_enabled_flag);
    if (gps.geom_planar_mode_enabled_flag) {
      bs.readUe(&gps.geom_planar_threshold0);
      bs.readUe(&gps.geom_planar_threshold1);
      bs.readUe(&gps.geom_planar_threshold2);
      bs.readUe(&gps.geom_planar_idcm_threshold);
    }

    gps.planar_buffer_disabled_flag = false;
    bs.read(&gps.geom_angular_mode_enabled_flag);
    if (gps.geom_angular_mode_enabled_flag) {
      Vec3<int> geom_angular_origin;
      bs.readUe(&geom_angular_origin.x());
      bs.readUe(&geom_angular_origin.y());
      bs.readUe(&geom_angular_origin.z());

      // NB: this is in XYZ axis order until the GPS is converted to STV
      gps.geomAngularOrigin = geom_angular_origin;

      int geom_angular_num_lidar_lasers;
      bs.readUe(&geom_angular_num_lidar_lasers);
      gps.geom_angular_theta_laser.resize(geom_angular_num_lidar_lasers);
      gps.geom_angular_z_laser.resize(geom_angular_num_lidar_lasers);
      gps.geom_angular_num_phi_per_turn.resize(geom_angular_num_lidar_lasers);

      if (geom_angular_num_lidar_lasers) {
        bs.readSe(&gps.geom_angular_theta_laser[0]);
        bs.readSe(&gps.geom_angular_z_laser[0]);
        bs.readUe(&gps.geom_angular_num_phi_per_turn[0]);
      }

      for (int i = 1; i < geom_angular_num_lidar_lasers; i++) {
        int geom_angular_theta_laser_diff;
        int geom_angular_z_laser_diff;
        bs.readUe(&geom_angular_theta_laser_diff);
        bs.readSe(&geom_angular_z_laser_diff);
        bs.readUe(&gps.geom_angular_num_phi_per_turn[i]);

        gps.geom_angular_theta_laser[i] =
          gps.geom_angular_theta_laser[i - 1] + geom_angular_theta_laser_diff;

        gps.geom_angular_z_laser[i] =
          gps.geom_angular_z_laser[i - 1] + geom_angular_z_laser_diff;
      }
      bs.read(&gps.planar_buffer_disabled_flag);
    }

    bs.readUe(&gps.neighbour_avail_boundary_log2);
    bs.readUe(&gps.intra_pred_max_node_size_log2);
    bs.readUe(&gps.trisoup_node_size_log2);

    gps.geom_base_qp = 0;
    gps.geom_idcm_qp_offset = 0;
    bs.read(&gps.geom_scaling_enabled_flag);
    if (gps.geom_scaling_enabled_flag) {
      bs.readUe(&gps.geom_base_qp);
      bs.readSe(&gps.geom_idcm_qp_offset);
    }
  }

  bool gps_extension_flag = bs.read();
  if (gps_extension_flag) {
    // todo(df): gps_extension_data;
    assert(!gps_extension_flag);
  }
  bs.byteAlign();

  return gps;
}

//----------------------------------------------------------------------------

void
convertXyzToStv(const SequenceParameterSet& sps, GeometryParameterSet* gps)
{
  gps->geomAngularOrigin =
    fromXyz(sps.geometry_axis_order, gps->geomAngularOrigin);
}

//============================================================================

PayloadBuffer
write(const SequenceParameterSet& sps, const AttributeParameterSet& aps)
{
  PayloadBuffer buf(PayloadType::kAttributeParameterSet);
  auto bs = makeBitWriter(std::back_inserter(buf));

  bs.writeUe(aps.aps_attr_parameter_set_id);
  bs.writeUe(aps.aps_seq_parameter_set_id);
  bs.writeUe(aps.attr_encoding);

  bs.writeUe(aps.init_qp_minus4);
  bs.writeSe(aps.aps_chroma_qp_offset);
  bs.write(aps.aps_slice_qp_deltas_present_flag);

  if (aps.lodParametersPresent()) {
    bs.writeUe(aps.num_pred_nearest_neighbours_minus1);
    bs.writeUe(aps.search_range);

    auto lod_neigh_bias = toXyz(sps.geometry_axis_order, aps.lodNeighBias);
    bs.writeUe(lod_neigh_bias.x());
    bs.writeUe(lod_neigh_bias.y());
    bs.writeUe(lod_neigh_bias.z());

    if (aps.attr_encoding == AttributeEncoding::kLiftingTransform) {
      bs.write(aps.scalable_lifting_enabled_flag);
      if (aps.scalable_lifting_enabled_flag)
        bs.writeUe(aps.max_neigh_range);
    }

    if (!aps.scalable_lifting_enabled_flag) {
      bs.writeUe(aps.num_detail_levels);
      if (!aps.num_detail_levels)
        bs.write(aps.canonical_point_order_flag);
      else {
        bs.write(aps.lod_decimation_enabled_flag);

        if (aps.lod_decimation_enabled_flag) {
          for (int idx = 0; idx < aps.num_detail_levels; idx++) {
            auto lod_sampling_period_minus2 = aps.lodSamplingPeriod[idx] - 2;
            bs.writeUe(lod_sampling_period_minus2);
          }
        } else {
          for (int idx = 0; idx < aps.num_detail_levels; idx++) {
            auto numerator = aps.dist2[idx];
            auto denominator = idx > 0 ? aps.dist2[idx - 1] : 1;
            int lod_sampling_scale_minus1 = (numerator / denominator) - 1;
            int lod_sampling_offset = numerator % denominator;
            bs.writeUe(lod_sampling_scale_minus1);
            if (idx > 0)
              bs.writeUe(lod_sampling_offset);
          }
        }
      }
    }
  }

  if (aps.attr_encoding == AttributeEncoding::kPredictingTransform) {
    bs.writeUe(aps.max_num_direct_predictors);
    if (aps.max_num_direct_predictors)
      bs.writeUe(aps.adaptive_prediction_threshold);
    bs.write(aps.intra_lod_prediction_enabled_flag);
    bs.write(aps.inter_component_prediction_enabled_flag);
  }

  if (aps.attr_encoding == AttributeEncoding::kRAHTransform) {
    bs.write(aps.raht_prediction_enabled_flag);
    if (aps.raht_prediction_enabled_flag) {
      bs.writeUe(aps.raht_prediction_threshold0);
      bs.writeUe(aps.raht_prediction_threshold1);
    }
  }

  bool aps_extension_flag = false;
  bs.write(aps_extension_flag);
  bs.byteAlign();

  return buf;
}

//----------------------------------------------------------------------------

AttributeParameterSet
parseAps(const PayloadBuffer& buf)
{
  AttributeParameterSet aps;
  assert(buf.type == PayloadType::kAttributeParameterSet);
  auto bs = makeBitReader(buf.begin(), buf.end());

  bs.readUe(&aps.aps_attr_parameter_set_id);
  bs.readUe(&aps.aps_seq_parameter_set_id);
  bs.readUe(&aps.attr_encoding);

  bs.readUe(&aps.init_qp_minus4);
  bs.readSe(&aps.aps_chroma_qp_offset);
  bs.read(&aps.aps_slice_qp_deltas_present_flag);

  if (aps.lodParametersPresent()) {
    bs.readUe(&aps.num_pred_nearest_neighbours_minus1);
    bs.readUe(&aps.search_range);

    Vec3<int> lod_neigh_bias;
    bs.readUe(&lod_neigh_bias.x());
    bs.readUe(&lod_neigh_bias.y());
    bs.readUe(&lod_neigh_bias.z());
    // NB: this is in XYZ axis order until the GPS is converted to STV
    aps.lodNeighBias = lod_neigh_bias;

    aps.scalable_lifting_enabled_flag = false;
    if (aps.attr_encoding == AttributeEncoding::kLiftingTransform) {
      bs.read(&aps.scalable_lifting_enabled_flag);
      if (aps.scalable_lifting_enabled_flag)
        bs.readUe(&aps.max_neigh_range);
    }

    aps.canonical_point_order_flag = false;
    if (!aps.scalable_lifting_enabled_flag) {
      bs.readUe(&aps.num_detail_levels);
      if (!aps.num_detail_levels)
        bs.read(&aps.canonical_point_order_flag);
      else {
        bs.read(&aps.lod_decimation_enabled_flag);

        if (aps.lod_decimation_enabled_flag) {
          aps.lodSamplingPeriod.resize(aps.num_detail_levels);
          for (int idx = 0; idx < aps.num_detail_levels; idx++) {
            int lod_sampling_period_minus2;
            bs.readUe(&lod_sampling_period_minus2);
            aps.lodSamplingPeriod[idx] = lod_sampling_period_minus2 + 2;
          }
        } else {
          aps.dist2.resize(aps.num_detail_levels);
          for (int idx = 0; idx < aps.num_detail_levels; idx++) {
            int lod_sampling_scale_minus1;
            int lod_sampling_offset = 0;
            bs.readUe(&lod_sampling_scale_minus1);
            if (idx == 0)
              aps.dist2[idx] = lod_sampling_scale_minus1 + 1;
            else {
              int lod_sampling_offset;
              bs.readUe(&lod_sampling_offset);
              aps.dist2[idx] =
                aps.dist2[idx - 1] * (lod_sampling_scale_minus1 + 1)
                + lod_sampling_offset;
            }
          }
        }
      }
    }
  }

  aps.intra_lod_prediction_enabled_flag = false;
  if (aps.attr_encoding == AttributeEncoding::kPredictingTransform) {
    bs.readUe(&aps.max_num_direct_predictors);
    aps.adaptive_prediction_threshold = 0;
    if (aps.max_num_direct_predictors)
      bs.readUe(&aps.adaptive_prediction_threshold);
    bs.read(&aps.intra_lod_prediction_enabled_flag);
    bs.read(&aps.inter_component_prediction_enabled_flag);
  }

  if (aps.attr_encoding == AttributeEncoding::kRAHTransform) {
    bs.read(&aps.raht_prediction_enabled_flag);
    if (aps.raht_prediction_enabled_flag) {
      bs.readUe(&aps.raht_prediction_threshold0);
      bs.readUe(&aps.raht_prediction_threshold1);
    }
  }

  bool aps_extension_flag = bs.read();
  if (aps_extension_flag) {
    // todo(df): aps_extension_data;
    assert(!aps_extension_flag);
  }
  bs.byteAlign();

  return aps;
}

//----------------------------------------------------------------------------

void
convertXyzToStv(const SequenceParameterSet& sps, AttributeParameterSet* aps)
{
  aps->lodNeighBias = fromXyz(sps.geometry_axis_order, aps->lodNeighBias);
}

//============================================================================

void
write(
  const SequenceParameterSet& sps,
  const GeometryParameterSet& gps,
  const GeometryBrickHeader& gbh,
  PayloadBuffer* buf)
{
  assert(buf->type == PayloadType::kGeometryBrick);
  auto bs = makeBitWriter(std::back_inserter(*buf));

  bs.writeUe(gbh.geom_geom_parameter_set_id);
  bs.writeUe(gbh.geom_tile_id);
  bs.writeUe(gbh.geom_slice_id);
  bs.writeUn(sps.log2_max_frame_idx, gbh.frame_idx);

  int geomBoxLog2Scale = gbh.geomBoxLog2Scale(gps);
  auto geom_box_origin = toXyz(sps.geometry_axis_order, gbh.geomBoxOrigin);
  geom_box_origin.x() >>= geomBoxLog2Scale;
  geom_box_origin.y() >>= geomBoxLog2Scale;
  geom_box_origin.z() >>= geomBoxLog2Scale;

  if (gps.geom_box_log2_scale_present_flag)
    bs.writeUe(gbh.geom_box_log2_scale);
  bs.writeUe(geom_box_origin.x());
  bs.writeUe(geom_box_origin.y());
  bs.writeUe(geom_box_origin.z());

  if (!gps.predgeom_enabled_flag) {
    int tree_depth_minus1 = gbh.tree_lvl_coded_axis_list.size() - 1;
    bs.writeUe(tree_depth_minus1);
    if (gps.qtbt_enabled_flag)
      for (int i = 0; i <= tree_depth_minus1; i++)
        bs.writeUn(3, gbh.tree_lvl_coded_axis_list[i]);

    bs.writeUe(gbh.geom_stream_cnt_minus1);
    if (gbh.geom_stream_cnt_minus1) {
      bs.writeUn(6, gbh.geom_stream_len_bits);

      // NB: the length of the last substream is not signalled
      for (int i = 0; i < gbh.geom_stream_cnt_minus1; i++)
        bs.writeUn(gbh.geom_stream_len_bits, gbh.geom_stream_len[i]);
    }

    if (gps.geom_scaling_enabled_flag) {
      bs.writeSe(gbh.geom_slice_qp_offset);
      bs.writeUe(gbh.geom_octree_qp_offset_depth);
    }

    if (gps.trisoup_node_size_log2) {
      bs.writeUe(gbh.trisoup_sampling_value_minus1);
      bs.writeUe(gbh.num_unique_segments_minus1);
    }
  }

  bs.byteAlign();
}

//----------------------------------------------------------------------------

GeometryBrickHeader
parseGbh(
  const SequenceParameterSet& sps,
  const GeometryParameterSet& gps,
  const PayloadBuffer& buf,
  int* bytesRead)
{
  GeometryBrickHeader gbh;
  assert(buf.type == PayloadType::kGeometryBrick);
  auto bs = makeBitReader(buf.begin(), buf.end());

  bs.readUe(&gbh.geom_geom_parameter_set_id);
  bs.readUe(&gbh.geom_tile_id);
  bs.readUe(&gbh.geom_slice_id);
  bs.readUn(sps.log2_max_frame_idx, &gbh.frame_idx);

  if (gps.geom_box_log2_scale_present_flag)
    bs.readUe(&gbh.geom_box_log2_scale);

  Vec3<int> geom_box_origin;
  bs.readUe(&geom_box_origin.x());
  bs.readUe(&geom_box_origin.y());
  bs.readUe(&geom_box_origin.z());
  gbh.geomBoxOrigin = fromXyz(sps.geometry_axis_order, geom_box_origin);
  gbh.geomBoxOrigin *= 1 << gbh.geomBoxLog2Scale(gps);

  if (!gps.predgeom_enabled_flag) {
    int tree_depth_minus1;
    bs.readUe(&tree_depth_minus1);

    gbh.tree_lvl_coded_axis_list.resize(tree_depth_minus1 + 1, 7);
    if (gps.qtbt_enabled_flag)
      for (int i = 0; i <= tree_depth_minus1; i++)
        bs.readUn(3, &gbh.tree_lvl_coded_axis_list[i]);

    bs.readUe(&gbh.geom_stream_cnt_minus1);
    if (gbh.geom_stream_cnt_minus1) {
      gbh.geom_stream_len.resize(gbh.geom_stream_cnt_minus1);

      bs.readUn(6, &gbh.geom_stream_len_bits);
      for (int i = 0; i < gbh.geom_stream_cnt_minus1; i++)
        bs.readUn(gbh.geom_stream_len_bits, &gbh.geom_stream_len[i]);
    }

    if (gps.geom_scaling_enabled_flag) {
      bs.readSe(&gbh.geom_slice_qp_offset);
      bs.readUe(&gbh.geom_octree_qp_offset_depth);
    }

    if (gps.trisoup_node_size_log2) {
      bs.readUe(&gbh.trisoup_sampling_value_minus1);
      bs.readUe(&gbh.num_unique_segments_minus1);
    }
  }

  bs.byteAlign();

  if (bytesRead)
    *bytesRead = int(std::distance(buf.begin(), bs.pos()));

  // To avoid having to make separate calls, the footer is parsed here
  gbh.footer = parseGbf(buf);

  return gbh;
}

//----------------------------------------------------------------------------

GeometryBrickHeader
parseGbhIds(const PayloadBuffer& buf)
{
  GeometryBrickHeader gbh;
  assert(buf.type == PayloadType::kGeometryBrick);
  auto bs = makeBitReader(buf.begin(), buf.end());

  bs.readUe(&gbh.geom_geom_parameter_set_id);
  bs.readUe(&gbh.geom_tile_id);
  bs.readUe(&gbh.geom_slice_id);

  /* NB: this function only decodes ids at the start of the header. */
  /* NB: do not attempt to parse any further */

  return gbh;
}

//============================================================================

void
write(const GeometryBrickFooter& gbf, PayloadBuffer* buf)
{
  assert(buf->type == PayloadType::kGeometryBrick);
  auto bs = makeBitWriter(std::back_inserter(*buf));

  // NB: if modifying this footer, it is essential that the decoder can
  // either decode backwards, or seek to the start.
  bs.writeUn(24, gbf.geom_num_points_minus1);
}

//----------------------------------------------------------------------------

GeometryBrickFooter
parseGbf(const PayloadBuffer& buf)
{
  GeometryBrickFooter gbf;
  assert(buf.type == PayloadType::kGeometryBrick);

  constexpr size_t kFooterLen = 3;
  auto bs = makeBitReader(std::prev(buf.end(), kFooterLen), buf.end());

  bs.readUn(24, &gbf.geom_num_points_minus1);

  return gbf;
}

//============================================================================

void
write(
  const SequenceParameterSet& sps,
  const AttributeParameterSet& aps,
  const AttributeBrickHeader& abh,
  PayloadBuffer* buf)
{
  assert(buf->type == PayloadType::kAttributeBrick);
  auto bs = makeBitWriter(std::back_inserter(*buf));

  bs.writeUe(abh.attr_attr_parameter_set_id);
  bs.writeUe(abh.attr_sps_attr_idx);
  bs.writeUe(abh.attr_geom_slice_id);

  if (aps.aps_slice_qp_deltas_present_flag) {
    bs.writeSe(abh.attr_qp_delta_luma);
    bs.writeSe(abh.attr_qp_delta_chroma);
  }

  bool attr_layer_qp_present_flag = !abh.attr_layer_qp_delta_luma.empty();
  bs.write(attr_layer_qp_present_flag);
  if (attr_layer_qp_present_flag) {
    int attr_num_qp_layers_minus1 = abh.attr_num_qp_layers_minus1();
    bs.writeUe(attr_num_qp_layers_minus1);
    for (int i = 0; i <= attr_num_qp_layers_minus1; i++) {
      bs.writeSe(abh.attr_layer_qp_delta_luma[i]);
      bs.writeSe(abh.attr_layer_qp_delta_chroma[i]);
    }
  }

  int attr_num_regions = abh.qpRegions.size();
  bs.writeUe(attr_num_regions);
  for (int i = 0; i < attr_num_regions; i++) {
    // NB: only one region is currently permitted.
    auto& region = abh.qpRegions[i];

    auto attr_region_origin =
      toXyz(sps.geometry_axis_order, region.regionOrigin);

    auto attr_region_whd_minus1 =
      toXyz(sps.geometry_axis_order, region.regionSize - 1);

    bs.writeUe(attr_region_origin.x());
    bs.writeUe(attr_region_origin.y());
    bs.writeUe(attr_region_origin.z());
    bs.writeUe(attr_region_whd_minus1.x());
    bs.writeUe(attr_region_whd_minus1.y());
    bs.writeUe(attr_region_whd_minus1.z());
    bs.writeSe(region.attr_region_qp_offset[0]);
    if (sps.attributeSets[abh.attr_sps_attr_idx].attr_num_dimensions_minus1)
      bs.writeSe(region.attr_region_qp_offset[1]);
  }
  bs.byteAlign();
}

//----------------------------------------------------------------------------

AttributeBrickHeader
parseAbhIds(const PayloadBuffer& buf)
{
  AttributeBrickHeader abh;
  assert(buf.type == PayloadType::kAttributeBrick);
  auto bs = makeBitReader(buf.begin(), buf.end());

  bs.readUe(&abh.attr_attr_parameter_set_id);
  bs.readUe(&abh.attr_sps_attr_idx);
  bs.readUe(&abh.attr_geom_slice_id);

  /* NB: this function only decodes ids at the start of the header. */
  /* NB: do not attempt to parse any further */

  return abh;
}

//----------------------------------------------------------------------------

AttributeBrickHeader
parseAbh(
  const SequenceParameterSet& sps,
  const AttributeParameterSet& aps,
  const PayloadBuffer& buf,
  int* bytesRead)
{
  AttributeBrickHeader abh;
  assert(buf.type == PayloadType::kAttributeBrick);
  auto bs = makeBitReader(buf.begin(), buf.end());

  bs.readUe(&abh.attr_attr_parameter_set_id);
  bs.readUe(&abh.attr_sps_attr_idx);
  bs.readUe(&abh.attr_geom_slice_id);

  if (aps.aps_slice_qp_deltas_present_flag) {
    bs.readSe(&abh.attr_qp_delta_luma);
    bs.readSe(&abh.attr_qp_delta_chroma);
  }

  bool attr_layer_qp_present_flag;
  bs.read(&attr_layer_qp_present_flag);
  if (attr_layer_qp_present_flag) {
    int attr_num_qp_layers_minus1;
    bs.readUe(&attr_num_qp_layers_minus1);
    abh.attr_layer_qp_delta_luma.resize(attr_num_qp_layers_minus1 + 1);
    abh.attr_layer_qp_delta_chroma.resize(attr_num_qp_layers_minus1 + 1);
    for (int i = 0; i <= attr_num_qp_layers_minus1; i++) {
      bs.readSe(&abh.attr_layer_qp_delta_luma[i]);
      bs.readSe(&abh.attr_layer_qp_delta_chroma[i]);
    }
  }

  // NB: Number of regions is restricted in this version of specification.
  int attr_num_regions;
  bs.readUe(&attr_num_regions);
  assert(attr_num_regions <= 1);
  abh.qpRegions.resize(attr_num_regions);
  for (int i = 0; i < attr_num_regions; i++) {
    auto& region = abh.qpRegions[i];
    Vec3<int> attr_region_origin;
    bs.readUe(&attr_region_origin.x());
    bs.readUe(&attr_region_origin.y());
    bs.readUe(&attr_region_origin.z());
    region.regionOrigin = fromXyz(sps.geometry_axis_order, attr_region_origin);

    Vec3<int> attr_region_whd_minus1;
    bs.readUe(&attr_region_whd_minus1.x());
    bs.readUe(&attr_region_whd_minus1.y());
    bs.readUe(&attr_region_whd_minus1.z());
    region.regionSize =
      fromXyz(sps.geometry_axis_order, attr_region_whd_minus1 + 1);

    bs.readSe(&region.attr_region_qp_offset[0]);
    if (sps.attributeSets[abh.attr_sps_attr_idx].attr_num_dimensions_minus1)
      bs.readSe(&region.attr_region_qp_offset[1]);
  }

  bs.byteAlign();

  if (bytesRead)
    *bytesRead = int(std::distance(buf.begin(), bs.pos()));

  return abh;
}

//============================================================================

ConstantAttributeDataUnit
parseConstantAttribute(
  const SequenceParameterSet& sps, const PayloadBuffer& buf)
{
  ConstantAttributeDataUnit cadu;
  assert(buf.type == PayloadType::kConstantAttribute);
  auto bs = makeBitReader(buf.begin(), buf.end());

  bs.readUe(&cadu.constattr_attr_parameter_set_id);
  bs.readUe(&cadu.constattr_sps_attr_idx);
  bs.readUe(&cadu.constattr_geom_slice_id);

  // todo(df): check bounds
  const auto& attrDesc = sps.attributeSets[cadu.constattr_sps_attr_idx];

  cadu.constattr_default_value.resize(attrDesc.attr_num_dimensions_minus1 + 1);
  bs.readUn(attrDesc.bitdepth, &cadu.constattr_default_value[0]);
  for (int k = 1; k <= attrDesc.attr_num_dimensions_minus1; k++)
    bs.readUn(attrDesc.bitdepthSecondary, &cadu.constattr_default_value[k]);

  return cadu;
}

//============================================================================

PayloadBuffer
write(const SequenceParameterSet& sps, const TileInventory& inventory)
{
  PayloadBuffer buf(PayloadType::kTileInventory);
  auto bs = makeBitWriter(std::back_inserter(buf));

  // todo(df): 7 is possible excessive, but is to maintain byte alignment
  bs.writeUn(7, inventory.ti_seq_parameter_set_id);
  bs.write(inventory.tile_id_present_flag);

  int num_tiles = inventory.tiles.size();
  bs.writeUn(16, num_tiles);

  // calculate the maximum size of any values
  int maxVal = 1;
  for (const auto& entry : inventory.tiles) {
    for (int k = 0; k < 3; k++) {
      maxVal = std::max(maxVal, entry.tileOrigin[k]);
      maxVal = std::max(maxVal, entry.tileSize[k]);
    }
  }

  int tile_bounding_box_bits = ceillog2(uint32_t(maxVal));
  bs.writeUn(8, tile_bounding_box_bits);

  for (const auto& entry : inventory.tiles) {
    if (inventory.tile_id_present_flag)
      bs.writeUe(entry.tile_id);

    auto tile_origin = toXyz(sps.geometry_axis_order, entry.tileOrigin);
    bs.writeSn(tile_bounding_box_bits, tile_origin.x());
    bs.writeSn(tile_bounding_box_bits, tile_origin.y());
    bs.writeSn(tile_bounding_box_bits, tile_origin.z());

    auto tile_size = toXyz(sps.geometry_axis_order, entry.tileSize);
    bs.writeUn(tile_bounding_box_bits, tile_size.x());
    bs.writeUn(tile_bounding_box_bits, tile_size.y());
    bs.writeUn(tile_bounding_box_bits, tile_size.z());
  }

  // NB: this is at the end of the inventory to aid fixed-width parsing
  auto ti_origin_xyz = toXyz(sps.geometry_axis_order, inventory.origin);
  bs.writeSe(ti_origin_xyz.x());
  bs.writeSe(ti_origin_xyz.y());
  bs.writeSe(ti_origin_xyz.z());

  int ti_origin_log2_scale = 0;
  bs.writeUe(ti_origin_log2_scale);

  bs.byteAlign();

  return buf;
}

//----------------------------------------------------------------------------

TileInventory
parseTileInventory(const PayloadBuffer& buf)
{
  TileInventory inventory;
  assert(buf.type == PayloadType::kTileInventory);
  auto bs = makeBitReader(buf.begin(), buf.end());

  bs.readUn(7, &inventory.ti_seq_parameter_set_id);
  bs.read(&inventory.tile_id_present_flag);

  int num_tiles;
  bs.readUn(16, &num_tiles);

  int tile_bounding_box_bits;
  bs.readUn(8, &tile_bounding_box_bits);

  for (int i = 0; i < num_tiles; i++) {
    int tile_id = i;
    if (inventory.tile_id_present_flag)
      bs.readUe(&tile_id);

    Vec3<int> tile_origin;
    bs.readSn(tile_bounding_box_bits, &tile_origin.x());
    bs.readSn(tile_bounding_box_bits, &tile_origin.y());
    bs.readSn(tile_bounding_box_bits, &tile_origin.z());
    Vec3<int> tile_size;
    bs.readUn(tile_bounding_box_bits, &tile_size.x());
    bs.readUn(tile_bounding_box_bits, &tile_size.y());
    bs.readUn(tile_bounding_box_bits, &tile_size.z());

    // NB: this is in XYZ axis order until the inventory is converted to STV
    TileInventory::Entry entry;
    entry.tile_id = tile_id;
    entry.tileOrigin = tile_origin;
    entry.tileSize = tile_size;
    inventory.tiles.push_back(entry);
  }

  Vec3<int> ti_origin_xyz;
  bs.readSe(&ti_origin_xyz.x());
  bs.readSe(&ti_origin_xyz.y());
  bs.readSe(&ti_origin_xyz.z());

  int ti_origin_log2_scale;
  bs.readUe(&ti_origin_log2_scale);
  ti_origin_xyz *= 1 << ti_origin_log2_scale;

  // NB: this is in XYZ axis order until converted to STV
  inventory.origin = ti_origin_xyz;

  bs.byteAlign();

  return inventory;
}

//----------------------------------------------------------------------------

void
convertXyzToStv(const SequenceParameterSet& sps, TileInventory* inventory)
{
  for (auto& tile : inventory->tiles) {
    tile.tileOrigin = fromXyz(sps.geometry_axis_order, tile.tileOrigin);
    tile.tileSize = fromXyz(sps.geometry_axis_order, tile.tileSize);
  }
}

//============================================================================

}  // namespace pcc
