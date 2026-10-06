export interface DependencyMessage {
  rootId: string;
}

export interface Env {
  DB: D1Database;
  STEAM_API_KEY?: string;
  SCRAPI?: string;
  SCRAPE_QUEUE?: Queue<DependencyMessage>;
  USE_QUEUE?: string;
  CONTROL_KEYS?: string;
}

export class HttpError extends Error {
  constructor(public readonly status: number, message: string) {
    super(message);
  }
}

export interface Author {
  id: string | null;
  name: string | null;
  profile_url: string | null;
}

export interface Preview {
  type: 'image' | 'video';
  url: string;
}

export interface SteamFile {
  publishedfileid: string;
  result: number;
  consumer_appid?: number;
  creator_appid?: number;
  creator?: string;
  title?: string;
  file_description?: string;
  description?: string;
  short_description?: string;
  file_type?: number;
  file_size?: string | number;
  preview_url?: string;
  time_created?: number;
  time_updated?: number;
  visibility?: number;
  banned?: boolean;
  incompatible?: boolean;
  subscriptions?: number;
  num_children?: number;
  tags?: Array<{ tag: string; display_name?: string }>;
  kvtags?: Array<{ key: string; value: string }>;
  children?: Array<{ publishedfileid: string; sortorder?: number; file_type?: number }>;
  previews?: Array<{ preview_type?: number; url?: string; youtubevideoid?: string; sortorder?: number }>;
  vote_data?: { votes_up?: number; votes_down?: number; score?: number };
}

export interface CatalogMod {
  id: string;
  kind: 'mod';
  source: 'steam';
  title: string;
  author: Author;
  description: string;
  description_format: 'bbcode';
  preview_url: string | null;
  previews: Preview[];
  workshop_url: string;
  tags: string[];
  supported_versions: string[];
  created_at: number | null;
  updated_at: number | null;
  file_size: string | null;
  subscriptions: number | null;
  votes: { up: number; down: number; positive_percent: number | null } | null;
  dependencies: string[];
  package_id: string | null;
  incompatible: boolean;
}

export interface CatalogCollection {
  id: string;
  kind: 'collection';
  source: 'steam' | 'picked';
  steam_id: string | null;
  title: string;
  author: Author;
  description: string;
  description_format: 'bbcode' | 'text';
  preview_url: string | null;
  previews: Preview[];
  workshop_url: string | null;
  tags: string[];
  supported_versions: string[];
  created_at: number | null;
  updated_at: number | null;
  member_ids: string[];
  member_count: number;
  member_previews: string[];
  /** Total download size in bytes; null until every member is known. */
  total_size: string | null;
}

export type CatalogItem = CatalogMod | CatalogCollection;

export interface CatalogPage<T> {
  items: T[];
  total: number;
  next_cursor: string | null;
}

export interface PickedCollectionInput {
  title: string;
  author: string;
  description: string;
  preview_url: string | null;
  tags: string[];
  supported_versions: string[];
  member_ids: string[];
  featured_rank: number;
}

export interface PickListOptions {
  query: string;
  tag: string | null;
  version: string | null;
  sort: 'featured' | 'updated' | 'newest' | 'name';
  limit: number;
  offset: number;
}
