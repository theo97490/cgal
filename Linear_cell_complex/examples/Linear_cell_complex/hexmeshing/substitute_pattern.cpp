#include <CGAL/Linear_cell_complex_for_combinatorial_map.h>
#include <CGAL/Linear_cell_complex_operations.h>
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/draw_linear_cell_complex.h>
#include <CGAL/Polyhedron_3.h>
#include <CGAL/AABB_tree.h>
#include <CGAL/AABB_traits.h>
#include <CGAL/AABB_face_graph_triangle_primitive.h>
#include <CGAL/Side_of_triangle_mesh.h>
#include <CGAL/Polygon_mesh_processing/triangulate_faces.h>
#include <CGAL/Aff_transformation_3.h>
#include <CGAL/aff_transformation_tags.h>

#include "../query_replace/cmap_query_replace.h"
#include "../query_replace/init_to_preserve_for_query_replace.h"

typedef CGAL::Exact_predicates_inexact_constructions_kernel Kernel;
typedef Kernel::FT       FT;
typedef Kernel::Point_3  Point;
typedef Kernel::Vector_3 Vector;
typedef CGAL::Linear_cell_complex_for_combinatorial_map<3,3> LCC;
typedef typename LCC::Dart_handle Dart_handle;
typedef typename LCC::Vertex_attribute_handle Vertex_handle;
typedef typename LCC::size_type   size_type;

const size_t SIZE_T_MAX = std::numeric_limits<size_t>::max();

// to be removed
size_type debug_edge_mark;

std::vector<Dart_handle> marked_cells_in_hex(LCC& lcc, Dart_handle& dart, size_type mark){
  auto checked_mark = lcc.get_new_mark();
  auto darts_of_volume = lcc.darts_of_cell<3>(dart);
  std::vector<Dart_handle> marked;
  marked.reserve(4);

  for (auto it = darts_of_volume.begin(); it != darts_of_volume.end(); it++){
    Dart_handle dit = it;

    if (lcc.is_marked(dit, checked_mark) || !lcc.is_marked(dit, mark)) continue;

    lcc.mark_cell<0>(dit, checked_mark);
    marked.push_back(dit);
  };

  lcc.free_mark(checked_mark);
  return marked;
}

void render(LCC &lcc, size_type marked_cell_0, size_type marked_cell_1)
{
  CGAL::Graphics_scene buffer;
  add_to_graphics_scene(lcc, buffer);

  // Hacking a bit here, we are going to draw colored points for marked vertices and edges just for debugging purposes
  // All of this is temporary of course and very ugly

  (const_cast<CGAL::Buffer_for_vao &>(buffer.get_buffer_for_mono_points())).clear();
  (const_cast<CGAL::Buffer_for_vao &>(buffer.get_buffer_for_mono_segments())).clear();

  // Marked vertices
  for (auto it = lcc.one_dart_per_cell<0>().begin(), end = lcc.one_dart_per_cell<0>().end(); it != end; it++)
  {

    if (lcc.is_marked(it, marked_cell_0))
    {
      buffer.add_point(
          lcc.point(it),
          CGAL::Color(0, 255, 0));
      continue;
    }

    buffer.add_point(lcc.point(it));
  }

  // Marked edges
  for (auto it = lcc.one_dart_per_cell<1>().begin(), end = lcc.one_dart_per_cell<1>().end(); it != end; it++){
    // lcc.is_whole_cell_marked<1>()
    if (lcc.is_whole_cell_marked<1>(it, marked_cell_1)){
      buffer.add_segment(lcc.point(it), lcc.point(lcc.other_extremity(it)), CGAL::Color(255, 0, 0));
      continue;
    }

    buffer.add_segment(lcc.point(it), lcc.point(lcc.other_extremity(it)));
  }


  draw_graphics_scene(buffer);
}

void mark_face(LCC& lcc, Dart_handle dart, size_type mark){
  auto it = lcc.darts_of_orbit<1>(dart).begin();
  auto end = lcc.darts_of_orbit<1>(dart).end();

  for (; it != end; it++){
    if (!lcc.is_whole_cell_marked<0>(it, mark)){
      lcc.mark_cell<0>(it, mark);
    }
  }
}

void mark_vol(LCC& lcc, Dart_handle dart, size_type mark){
  auto it = lcc.darts_of_orbit<1,2>(dart).begin();
  auto end = lcc.darts_of_orbit<1,2>(dart).end();

  for (; it != end; it++){
    if (!lcc.is_whole_cell_marked<0>(it, mark)){
      lcc.mark_cell<0>(it, mark);
    }
  }
}

void mark_edge(LCC& lcc, Dart_handle dart, size_type mark){
  if (!lcc.is_whole_cell_marked<0>(dart, mark)){
    lcc.mark_cell<0>(dart, mark);
  }

  auto ext = lcc.other_extremity(dart);
  if (!lcc.is_whole_cell_marked<0>(ext, mark)){
    lcc.mark_cell<0>(ext, mark);
  }
}

Dart_handle find_3_template_origin(LCC& lcc, std::vector<Dart_handle> &mcells_in_hex, size_type corner_mark) {
  Dart_handle d1 = mcells_in_hex[0];
  Dart_handle d2 = mcells_in_hex[1];
  Dart_handle dart = d1;

  // If the two marks aren't on the same face, they must be one face away 
  if (!lcc.belong_to_same_cell<2>(dart, d2)){
    dart = lcc.beta(dart, 2);
    assert(lcc.belong_to_same_cell<2>(dart, d2));
  }

  // Get the origin dart : Find the two unmarked node on the face 
  // since the 3 template is created by adjacent two 2-templates
  bool found = false;
  for (int i = 0; i < 6; i++)
  {
    Dart_handle other_d_nonmark = lcc.beta(dart, 1, 1);
    if (!lcc.is_marked(dart, corner_mark) && !lcc.is_marked(other_d_nonmark, corner_mark))
    {
      found = true;
      break;
    }

    dart = lcc.beta(dart, 1);
  }

  assert(found);

  return dart;
}

void refine_3_template(LCC &lcc, std::vector<Dart_handle> &mcells_in_hex, Pattern_substituer<LCC> &pss, size_type corner_mark)
{
  Dart_handle origin_dart = find_3_template_origin(lcc, mcells_in_hex, corner_mark);  

  Dart_handle upper_d1 = origin_dart;
  Dart_handle upper_d2 = lcc.beta(origin_dart, 1, 1);

  assert(!lcc.is_marked(upper_d2, corner_mark));

  Dart_handle upper_edge = lcc.insert_cell_1_in_cell_2(upper_d1, upper_d2);

  // Query replace with the partial 3-template, making it into two volumes
  pss.query_replace_one_volume(lcc, origin_dart);

  // Face of the t<o neighboring volumes to the created volume
  Dart_handle face1 = lcc.beta(origin_dart, 2, 3);
  Dart_handle face2 = lcc.beta(origin_dart, 1, 2, 3);
  // lower_edge is created with the query_replace
  Dart_handle lower_edge = lcc.beta(upper_edge, 2, 1, 1);

  // Contract upper and lower edge into its barycenter 

  Dart_handle upper_mid_1 = lcc.insert_barycenter_in_cell<1>(upper_edge);
  Dart_handle upper_mid_2 = lcc.beta(upper_mid_1, 2, 1);
  Dart_handle lower_mid_1 = lcc.insert_barycenter_in_cell<1>(lower_edge);
  Dart_handle lower_mid_2 = lcc.beta(lower_mid_1, 2, 1);

  assert(!lcc.is_marked(upper_mid_2, corner_mark));
  assert(!lcc.is_marked(lower_mid_2, corner_mark));

  lcc.contract_cell<1>(upper_mid_1);
  lcc.contract_cell<1>(upper_mid_2);
  lcc.contract_cell<1>(lower_mid_1);
  lcc.contract_cell<1>(lower_mid_2);

  // Contract the two remaining 2-darts faces 

  Dart_handle face_to_remove_1 = lcc.beta(upper_edge, 2, 1);
  Dart_handle face_to_remove_2 = lcc.beta(upper_edge, 2, 1, 1, 2, 1, 2);

  assert(lcc.darts_of_orbit<1>(face_to_remove_1).size() == 2);
  assert(lcc.darts_of_orbit<1>(face_to_remove_2).size() == 2);

  lcc.contract_cell<2>(face_to_remove_1);
  lcc.contract_cell<2>(face_to_remove_2);

  // Remove the created volume and sew the two neighboring volumes
  lcc.remove_cell<3>(upper_d1);
  lcc.sew<3>(face1, face2);
}

void refine_marked_hexes(LCC &lcc, Pattern_substituer<LCC> &ps, Pattern_substituer<LCC> &pss, size_type toprocess_mark, size_type corner_mark)
{
  // Replace all faces that has a matching fpattern
  std::vector<Dart_handle> marked_cells;


  for (auto dit = lcc.one_dart_per_cell<2>().begin(),
            dend = lcc.one_dart_per_cell<2>().end();
       dit != dend;
       dit++)
  {
    Dart_handle dart = dit;
    marked_cells.push_back(dart);
  }


  int nbsub = 0;
  for (auto& dart : marked_cells)
  {
    if (ps.query_replace_one_face(lcc, dart, toprocess_mark) != SIZE_T_MAX) nbsub++;
  }

  std::cout << nbsub << " Faces substitution was made" << std::endl;

  // Refine the resulting volumes
  marked_cells.clear();

  using Volume_3_Set = std::vector<std::vector<Dart_handle>>;
  std::vector<Dart_handle>& volumes_regular_template = marked_cells;
  Volume_3_Set volumes_3_template;

  for (auto dit = lcc.one_dart_per_cell<3>().begin(),
            dend = lcc.one_dart_per_cell<3>().end();
       dit != dend;
       dit++)
  {
    Dart_handle dart = dit;
    std::vector mcell_in_hex  = marked_cells_in_hex(lcc, dart, toprocess_mark);

    if (mcell_in_hex.size() == 3){
      volumes_3_template.push_back(mcell_in_hex);
      continue;
    }

    marked_cells.push_back(dart);
  }

  nbsub = 0;
  for (auto& dart : volumes_regular_template)
  {
    if (ps.query_replace_one_volume(lcc, dart) != SIZE_T_MAX) nbsub++;
  }

  // Refine remaining 3 patterns

  // Find the four middle vertices
  for (auto mcells_in_hex : volumes_3_template){
    refine_3_template(lcc, mcells_in_hex, pss, corner_mark);
    nbsub++;
  }

  std::cout << nbsub << " volumic substitution was made" << std::endl;
}

void create_vertices_for_templates(LCC &lcc, std::vector<Dart_handle>& marked_cells, size_type toprocess_mark)
{

  // 2 noeuds marqué l'un à coté de l'autre ne produit pas de sommet
  // 1 noeud marqué a coté d'un noeud non marqué produit un sommet

  // TODO A changer pour une itération sur les arretes ou pas selon comment l'algo va s'executer

  std::vector<Dart_handle> edges_to_subdivide;

  auto arrete_done = lcc.get_new_mark();

  for (auto dart : marked_cells)
  {
    for (auto nit = lcc.one_dart_per_incident_cell<1, 0>(dart).begin(),
              nend = lcc.one_dart_per_incident_cell<1, 0>(dart).end();
         nit != nend;
         nit++)
    {
      if (lcc.is_marked(nit, arrete_done))
        continue;

      // If the node is next to an other marked node, we don't have to create vertices
      if (lcc.is_marked(lcc.beta<1>(nit), toprocess_mark)){
        lcc.mark_cell<1>(nit, arrete_done);
        continue;
      }

      edges_to_subdivide.push_back(nit);
      lcc.mark_cell<1>(nit, arrete_done);
    }

    dart = lcc.beta<1>(dart);
  }

  for (Dart_handle dart : edges_to_subdivide)
  {
    lcc.insert_barycenter_in_cell<1>(dart);
  }

  lcc.free_mark(arrete_done);
}

void mark_1template_face(LCC& lcc, size_type mark){
  auto dart = lcc.one_dart_per_cell<3>().begin();
  lcc.mark_cell<0>(lcc.beta(dart, 0, 2, 0, 0), mark);
}

void mark_2template_face(LCC& lcc, size_type mark){
  auto dart = lcc.one_dart_per_cell<3>().begin();
  lcc.mark_cell<0>(dart, mark);
  lcc.mark_cell<0>(lcc.beta(dart, 1), mark);
}


int main()
{
  LCC lcc;
  Pattern_substituer<LCC> ps, pss;
  auto d1=
    lcc.make_hexahedron(Point(0,0,0), Point(5,0,0),
                        Point(5,5,0), Point(0,5,0),
                        Point(0,5,5), Point(0,0,5),
                        Point(5,0,5), Point(5,5,5));

  auto d2=
    lcc.make_hexahedron(Point(5,0,0), Point(10,0,0),
                        Point(10,5,0), Point(5,5,0),
                        Point(5,5,5), Point(5,0,5),
                        Point(10,0,5), Point(10,5,5));

  auto d3=
    lcc.make_hexahedron(Point(0,0,-5), Point(5,0,-5),
                        Point(5,5,-5), Point(0,5,-5),
                        Point(0,5,0), Point(0,0,0),
                        Point(5,0,0), Point(5,5,0));

  lcc.sew<3>(lcc.beta(d1, 1, 1, 2), lcc.beta(d2, 2));
  lcc.sew<3>(lcc.beta(d1, 0, 2), lcc.beta(d3, 1, 2));

  ps.load_vpatterns(CGAL::data_file_path("hexmeshing/vpattern/complete"));

  // BUG Manque des opérateurs de déplacements pour Pattern, le reserve est un fix temporaire
  // Pour pouvoir charger les patterns correctement sans réallocation
  ps.m_fpatterns.reserve(10);
  ps.load_additional_fpattern(CGAL::data_file_path("hexmeshing/fpattern/pattern1-face.moka"), mark_1template_face);
  ps.load_additional_fpattern(CGAL::data_file_path("hexmeshing/fpattern/pattern2-face.moka"), mark_2template_face);

  // Load seperatly the partial 3 template
  pss.load_vpatterns(CGAL::data_file_path("hexmeshing/vpattern/partial"));

  lcc.display_characteristics(std::cout);

  auto toprocess_mark = lcc.get_new_mark();
  auto corner_mark = lcc.get_new_mark();
  debug_edge_mark = lcc.get_new_mark();

  lcc.negate_mark(corner_mark);

  lcc.mark_cell<0>(d1, toprocess_mark);
  auto md1 = lcc.beta(d1, 1);
  lcc.mark_cell<0>(md1, toprocess_mark);
  auto md2 = lcc.beta(d2, 1);
  lcc.mark_cell<0>(md2, toprocess_mark);
  auto md3 = lcc.beta(d2, 1, 1);
  lcc.mark_cell<0>(md3, toprocess_mark);
  auto md4 = d3;
  lcc.mark_cell<0>(md4, toprocess_mark);

  // lcc.mark_cell<0>(md3, toprocess_mark);

  std::vector<Dart_handle> marked_cells;

  // We assume we already filled out marked_cells earlier

  marked_cells.push_back(d1);
  marked_cells.push_back(md1);
  marked_cells.push_back(md2);
  marked_cells.push_back(md3);
  marked_cells.push_back(md4);

  create_vertices_for_templates(lcc, marked_cells, toprocess_mark);
  refine_marked_hexes(lcc, ps, pss, toprocess_mark, corner_mark);

  render(lcc, toprocess_mark, debug_edge_mark);
  return EXIT_SUCCESS;
}